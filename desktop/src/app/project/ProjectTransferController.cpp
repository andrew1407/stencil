#include "ProjectTransferController.hpp"
#include "projectTransferParts.hpp"
#include "../../support/rowWork.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "ServerClient.hpp"
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <algorithm>
#include <optional>

namespace stencil::gui {

  ProjectTransferController::ProjectTransferController(Notifications* notify, CanvasWidget* canvas,
                                                      const Settings* settings,
                                                      core::ProjectsStore* store,
                                                      std::vector<Project>* projectList, Hooks hooks)
      : notify(notify), canvas(canvas), settings(settings), store(store),
        projectList(projectList), h(std::move(hooks)) {}

  stencil::net::ServerClient* ProjectTransferController::requireClient(const QString& url) {
    stencil::net::ConnectionManager* mgr = this->h.connections();
    stencil::net::ServerClient* c = mgr ? mgr->find(url) : nullptr;
    if (!c) {
      notify->error("Not connected to that server");
      return nullptr;
    }
    return c;
  }

  void ProjectTransferController::localProjectOriginal(const Project& pr,
                                                       std::function<void(const Original&)> done) {
    if (QString::fromStdString(pr.meta.id) == this->h.activeProjectId() && canvas->hasImage()) {
      const QImage img = canvas->getImage();
      support::runOnPool<Original>(canvas, [img] {
        return Original{pngBytes(img), QStringLiteral("png"), img.width(), img.height()};
      }, std::move(done));
      return;
    }
    if (pr.imagePath.isEmpty()) {
      notify->error("This project has no stored image");
      return;
    }
    const QString path = pr.imagePath;
    support::runOnPool<std::optional<Original>>(canvas, [path]() -> std::optional<Original> {
      const QImage img(path);
      if (img.isNull()) return std::nullopt;
      Original out{{}, QStringLiteral("png"), img.width(), img.height()};
      QFile f(path);
      if (f.open(QIODevice::ReadOnly)) {
        out.bytes = f.readAll();
        const QString suf = QFileInfo(path).suffix().toLower();
        if (!suf.isEmpty()) out.ext = suf;
      }
      if (out.bytes.isEmpty()) out.bytes = pngBytes(img);  // unreadable file → re-encode the decoded image
      return out;
    }, [this, done = std::move(done)](std::optional<Original> read) {
      if (!read) {
        notify->error("Could not read the project image");
        return;
      }
      done(*read);
    });
  }

  // Lifetime: every REST reply is bound to `c`'s network-access-manager, which outlives this
  // controller, so a reply never fires after `this` dies. Same for every method below.
  void ProjectTransferController::createServerFromLocal(
      stencil::net::ServerClient* c, const Project& pr, const QString& name, const QByteArray& bytes,
      const QString& ext, int w, int h,
      std::function<void(bool ok, QString newId, qint64 newVersion)> done) {
    // pr may not outlive the chain.
    const QJsonObject layout = fileStore::buildLayoutJson(
        w, h, pr.lines, settings->imageFilter, settings->filterColor,
        pr.cropRect, pr.rotationQuarters, this->h.currentLayoutMeta(), pr.mirrored);
    c->createProjectAsync(
        name, QString::fromStdString(pr.meta.source), QString::fromStdString(pr.meta.resource), true,
        w, h, [this, c, name, bytes, ext, w, h, layout, done](bool ok, QString newId, qint64) {
          if (!ok) {
            notify->error(QString("Could not create on server — %1").arg(c->lastError()));
            done(false, QString(), 0);
            return;
          }
          c->uploadFileAsync(newId, "original", bytes, ext, w, h,
                             [this, c, newId, name, layout, done](bool uok) {
            if (!uok) {
              notify->error(QString("Created, but image upload failed — %1").arg(c->lastError()));
              done(false, QString(), 0);
              return;
            }
            // The upload bumped the version, and the layout PUT is guarded: send the one it left.
            c->getProjectAsync(newId, [this, c, newId, name, layout, done](
                                          bool gok, stencil::net::ServerProject meta, QJsonObject) {
              auto failed = [this, c, done] {
                notify->error(QString("Created, but the layout was not saved — %1").arg(c->lastError()));
                done(false, QString(), 0);
              };
              if (!gok) return failed();
              c->updateProjectAsync(newId, name, layout, meta.version,
                                    [newId, done, failed](bool pok, qint64 nv, bool /*conflict*/) {
                if (!pok) return failed();
                done(true, newId, nv);
              });
            });
          });
        });
  }

  // Mirrors the browser's moveProjectToServer().
  void ProjectTransferController::moveLocalProjectToServer(const QString& serverUrl,
                                                           const QString& id) {
    if (!requireClient(serverUrl)) return;
    Project* found = this->h.findProject(id.toStdString());
    if (!found) {
      notify->error("Project not found");
      return;
    }
    localProjectOriginal(*found, [this, serverUrl, id](const Original& o) {
      stencil::net::ServerClient* c = requireClient(serverUrl);
      Project* pr = c ? this->h.findProject(id.toStdString()) : nullptr;
      if (!pr) return;   // disconnected, or removed while its picture was read
      const QString name = QString::fromStdString(pr->meta.name);
      // create cannot set the colour.
      const QString localColor = QString::fromStdString(pr->meta.color);
      const std::string sid = id.toStdString();
      const bool wasActive = (this->h.activeProjectId() == id);
      auto finish = [this, sid, name, serverUrl, localColor, wasActive](const QString& newId,
                                                                        qint64 newVersion) {
        projectList->erase(
            std::remove_if(projectList->begin(), projectList->end(),
                           [&](const Project& p) { return p.meta.id == sid; }),
            projectList->end());
        fileStore::saveProjects(*projectList);
        // Keep the editor open and link the live session to the new server project instead of
        // orphaning the canvas.
        if (wasActive) this->h.relinkActiveToServer(serverUrl, newId, name, localColor, newVersion);
        this->h.afterChange();
        notify->success(QString("Moved \"%1\" to %2").arg(name, serverUrl));
      };
      createServerFromLocal(c, *pr, name, o.bytes, o.ext, o.w, o.h,
                            [this, c, localColor, finish](bool ok, QString newId, qint64 newVersion) {
        if (!ok) return;
        if (localColor.isEmpty()) {
          finish(newId, newVersion);
          return;
        }
        c->updateProjectColorAsync(newId, localColor, newVersion,
                                   [this, c, finish, newId, newVersion](bool cok, qint64 nv, bool) {
          // The project and its layout are there; only the colour is missing, so it still moves.
          if (!cok) notify->error(QString("Moved, but the colour was not saved — %1").arg(c->lastError()));
          finish(newId, cok ? nv : newVersion);
        });
      });
    });
  }
}  // namespace stencil::gui


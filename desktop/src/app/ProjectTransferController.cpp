#include "ProjectTransferController.hpp"
#include "projectTransferParts.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "ServerClient.hpp"
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <QRandomGenerator>
#include <algorithm>

namespace stencil::gui {


  ProjectTransferController::ProjectTransferController(Notifications* notify, CanvasWidget* canvas,
                                                      const Settings* settings,
                                                      core::ProjectsStore* store,
                                                      std::vector<Project>* projectList, Hooks hooks)
      : notify_(notify), canvas_(canvas), settings_(settings), store_(store),
        projectList_(projectList), h_(std::move(hooks)) {}

  stencil::net::ServerClient* ProjectTransferController::requireClient(const QString& url) {
    stencil::net::ConnectionManager* mgr = h_.connections();
    stencil::net::ServerClient* c = mgr ? mgr->find(url) : nullptr;
    if (!c) {
      notify_->error("Not connected to that server");
      return nullptr;
    }
    return c;
  }

  bool ProjectTransferController::localProjectOriginal(const Project& pr, QByteArray& bytes,
                                                       QString& ext, int& w, int& h) {
    ext = "png";
    if (QString::fromStdString(pr.meta.id) == h_.activeProjectId() && canvas_->hasImage()) {
      const QImage img = canvas_->image();
      bytes = pngBytes(img);
      w = img.width();
      h = img.height();
      return true;
    }
    if (pr.imagePath.isEmpty()) {
      notify_->error("This project has no stored image");
      return false;
    }
    const QImage img(pr.imagePath);
    if (img.isNull()) {
      notify_->error("Could not read the project image");
      return false;
    }
    w = img.width();
    h = img.height();
    QFile f(pr.imagePath);
    if (f.open(QIODevice::ReadOnly)) {
      bytes = f.readAll();
      f.close();
      const QString suf = QFileInfo(pr.imagePath).suffix().toLower();
      if (!suf.isEmpty()) ext = suf;
    }
    if (bytes.isEmpty()) bytes = pngBytes(img);  // unreadable file → re-encode the decoded image
    return true;
  }

  // Lifetime: every REST reply is bound to `c`'s network-access-manager, owned by the
  // ConnectionManager under the MainWindow that owns this controller, so a reply never fires after
  // `this` dies. Same for every method below.
  void ProjectTransferController::createServerFromLocal(
      stencil::net::ServerClient* c, const Project& pr, const QString& name, const QByteArray& bytes,
      const QString& ext, int w, int h,
      std::function<void(bool ok, QString newId, qint64 newVersion)> done) {
    // pr may not outlive the chain.
    const QJsonObject layout = fileStore::buildLayoutJson(
        w, h, pr.lines, settings_->imageFilter, settings_->filterColor,
        pr.cropRect, pr.rotationQuarters, h_.currentLayoutMeta());
    c->createProjectAsync(
        name, QString::fromStdString(pr.meta.source), QString::fromStdString(pr.meta.resource), true,
        w, h, [this, c, name, bytes, ext, w, h, layout, done](bool ok, QString newId, qint64 version) {
          if (!ok) {
            notify_->error(QString("Could not create on server — %1").arg(c->lastError()));
            done(false, QString(), 0);
            return;
          }
          c->uploadFileAsync(newId, "original", bytes, ext, w, h,
                             [this, c, newId, name, layout, version, done](bool uok) {
            if (!uok) {
              notify_->error(QString("Created, but image upload failed — %1").arg(c->lastError()));
              done(false, QString(), 0);
              return;
            }
            // Single-shot write: a freshly created project has no concurrent editors, so a 409 is
            // left as-is.
            c->updateProjectAsync(newId, name, layout, version,
                                  [newId, version, done](bool pok, qint64 nv, bool /*conflict*/) {
              done(true, newId, pok ? nv : version);
            });
          });
        });
  }

  // Mirrors the browser's moveProjectToServer().
  void ProjectTransferController::moveLocalProjectToServer(const QString& serverUrl,
                                                           const QString& id) {
    stencil::net::ServerClient* c = requireClient(serverUrl);
    if (!c) return;
    Project* pr = h_.findProject(id.toStdString());
    if (!pr) {
      notify_->error("Project not found");
      return;
    }
    QByteArray bytes;
    QString ext;
    int w = 0;
    int h = 0;
    if (!localProjectOriginal(*pr, bytes, ext, w, h)) return;
    const QString name = QString::fromStdString(pr->meta.name);
    // create cannot set the colour.
    const QString localColor = QString::fromStdString(pr->meta.color);
    const std::string sid = id.toStdString();
    const bool wasActive = (h_.activeProjectId() == id);
    auto finish = [this, sid, name, serverUrl, localColor, wasActive](const QString& newId,
                                                                      qint64 newVersion) {
      projectList_->erase(
          std::remove_if(projectList_->begin(), projectList_->end(),
                         [&](const Project& p) { return p.meta.id == sid; }),
          projectList_->end());
      fileStore::saveProjects(*projectList_);
      // Keep the editor open and link the live session to the new server project instead of
      // orphaning the canvas.
      if (wasActive) h_.relinkActiveToServer(serverUrl, newId, name, localColor, newVersion);
      h_.afterChange();
      notify_->success(QString("Moved \"%1\" to %2").arg(name, serverUrl));
    };
    createServerFromLocal(c, *pr, name, bytes, ext, w, h,
                          [c, localColor, finish](bool ok, QString newId, qint64 newVersion) {
      if (!ok) return;
      if (localColor.isEmpty()) {
        finish(newId, newVersion);
        return;
      }
      c->updateProjectColorAsync(newId, localColor, newVersion,
                                 [finish, newId, newVersion](bool cok, qint64 nv, bool /*conflict*/) {
        finish(newId, cok ? nv : newVersion);  // keep the create's version if the colour PUT 409s
      });
    });
  }
}  // namespace stencil::gui


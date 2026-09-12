#include "projectTransferController.hpp"
#include "projectTransferParts.hpp"
#include "canvasWidget.hpp"
#include "notifications.hpp"
#include "serverClient.hpp"
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

  // Create `pr` on the server under `name`: upload the original bytes, then push the annotated
  // layout (lines + filter + page/formulas) so the server holds the full project. Reports
  // (ok, newId, newVersion) via `done`; notifies on failure.
  //
  // LIFETIME: the completion lambdas capture the controller's `this`, but every REST reply is bound
  // to `c`'s network-access-manager. `c` is owned by the ConnectionManager, which is owned by the
  // MainWindow that owns this controller — so a reply fires only while `c` (hence this controller)
  // is alive. If the server is disconnected (or the window closed) mid-transfer, `c` dies, the
  // reply is severed, and the chain is a safe no-op (the transfer simply stops). Same holds for
  // every method below.
  void ProjectTransferController::createServerFromLocal(
      stencil::net::ServerClient* c, const Project& pr, const QString& name, const QByteArray& bytes,
      const QString& ext, int w, int h,
      std::function<void(bool ok, QString newId, qint64 newVersion)> done) {
    // Copy the layout inputs the async tail needs (pr may not outlive the chain).
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
            // Single-shot guarded write (no retry): a freshly created project has no concurrent
            // editors, so a 409 is left as-is (newVersion keeps the create's version), matching the
            // previous fire-and-forget updateProject call.
            c->updateProjectAsync(newId, name, layout, version,
                                  [newId, version, done](bool pok, qint64 nv, bool /*conflict*/) {
              done(true, newId, pok ? nv : version);
            });
          });
        });
  }

  // Local → server: create the project on `serverUrl`, upload its original image, push the
  // annotated layout, then drop the local copy. Mirrors the browser's moveProjectToServer().
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
    // Carry the project's accent colour onto the server copy (create can't set it).
    const QString localColor = QString::fromStdString(pr->meta.color);
    const std::string sid = id.toStdString();
    const bool wasActive = (h_.activeProjectId() == id);
    // Drop the now-redundant local copy, link the editor if it was open, then notify.
    auto finish = [this, sid, name, serverUrl, localColor, wasActive](const QString& newId,
                                                                      qint64 newVersion) {
      projectList_->erase(
          std::remove_if(projectList_->begin(), projectList_->end(),
                         [&](const Project& p) { return p.meta.id == sid; }),
          projectList_->end());
      fileStore::saveProjects(*projectList_);
      // If it was the open project, keep the editor open and LINK the live session to the new
      // server project (golden frame) instead of orphaning the canvas.
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


#include "projectTransferController.hpp"
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

  namespace {
    long long nowMs() { return QDateTime::currentMSecsSinceEpoch(); }
    std::string makeSalt() {
      return QString::number(QRandomGenerator::global()->bounded(1 << 24), 36).toStdString();
    }
    // Encode a QImage as PNG bytes for upload (the server is codec-free, so the desktop hands it
    // already-encoded image bytes + the dimensions separately).
    QByteArray pngBytes(const QImage& img) {
      QByteArray out;
      QBuffer buf(&out);
      buf.open(QIODevice::WriteOnly);
      img.save(&buf, "PNG");
      return out;
    }
  }  // namespace

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

  // Local → server COPY: create a new server project from a local one (default name
  // "<name>-copy"), leaving the local project in place. Mirrors browser copyProjectToServer.
  void ProjectTransferController::copyLocalProjectToServer(const QString& serverUrl,
                                                           const QString& id, const QString& name) {
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
    const QString copyName = name.trimmed().isEmpty()
                                 ? (QString::fromStdString(pr->meta.name) + "-copy")
                                 : name.trimmed();
    createServerFromLocal(c, *pr, copyName, bytes, ext, w, h,
                          [this, copyName, serverUrl](bool ok, QString, qint64) {
      if (!ok) return;
      h_.afterChange();
      notify_->success(QString("Copied \"%1\" to %2").arg(copyName, serverUrl));
    });
  }

  // Server → local: download the project's image + layout, persist it as a new local project,
  // then delete it from the server. Mirrors moveProjectToLocal().
  void ProjectTransferController::moveServerProjectToLocal(const QString& serverUrl,
                                                           const QString& id) {
    // If this server project is the open remote session, follow it to local so the editor stays
    // open + focused instead of pointing at the deleted server id.
    const bool wasOpen = (h_.remoteId() == id && h_.remoteAddress() == serverUrl);
    importServerProjectToLocal(serverUrl, id, /*removeFromServer=*/true, "",
                               [this, wasOpen](bool ok, QString newId) {
      if (!ok) return;
      if (wasOpen) h_.loadProjectIntoCanvas(newId);  // rebind the editor to the new local project
      h_.afterChange();
      notify_->success("Moved to local storage");
    });
  }

  void ProjectTransferController::makeLocalCopyOfServerProject(const QString& serverUrl,
                                                              const QString& id,
                                                              const QString& name) {
    importServerProjectToLocal(serverUrl, id, /*removeFromServer=*/false, name,
                               [this](bool ok, QString newId) {
      if (!ok) return;
      h_.afterChange();
      h_.loadProjectIntoCanvas(newId);  // open the detached copy (clears the remote link)
      notify_->success("Local copy created");
    });
  }

  // Shared body: fetch a server project's image + layout (incl. crop/rotation), persist a fresh
  // detached local project; optionally delete the server copy. `name` (when non-empty) overrides
  // the server's name (used for the copy's "<name>-copy"). Errors are reported; reports
  // (ok, newLocalId) via `done`. Async: getProject → downloadFile("original") → (on empty)
  // fetchUrlBytes(source) → decode+persist → (removeFromServer) deleteProject.
  void ProjectTransferController::importServerProjectToLocal(
      const QString& serverUrl, const QString& id, bool removeFromServer, const QString& name,
      std::function<void(bool ok, QString newId)> done) {
    stencil::net::ServerClient* c = requireClient(serverUrl);
    if (!c) { if (done) done(false, QString()); return; }
    c->getProjectAsync(id, [this, c, id, name, removeFromServer, done](
                               bool gok, stencil::net::ServerProject meta, QJsonObject layout) {
      if (!gok) {
        notify_->error(QString("Could not fetch server project — %1").arg(c->lastError()));
        if (done) done(false, QString());
        return;
      }
      // Decode `bytes`, persist a fresh local project, optionally delete the server copy, report.
      auto persist = [this, c, id, name, removeFromServer, meta, layout, done](QByteArray bytes) {
        if (bytes.isEmpty()) {
          notify_->error("Server project has no image");
          if (done) done(false, QString());
          return;
        }
        QImage img;
        if (!img.loadFromData(bytes)) {
          notify_->error("Server image could not be decoded");
          if (done) done(false, QString());
          return;
        }
        // Persist the bytes to a file under the state dir so the local project reloads its pixels
        // on open (local projects reference an on-disk imagePath).
        Project pr;
        pr.meta.id = store_->createId(nowMs(), makeSalt());
        const QString imgDir = fileStore::stateDir() + "/images";
        QDir().mkpath(imgDir);
        const QString path = imgDir + "/" + QString::fromStdString(pr.meta.id) + ".png";
        if (!img.save(path, "PNG")) {
          notify_->error("Could not write the image to local storage");
          if (done) done(false, QString());
          return;
        }
        const QString baseName = meta.name.isEmpty() ? QStringLiteral("Untitled") : meta.name;
        pr.meta.name = (name.trimmed().isEmpty() ? baseName : name.trimmed()).toStdString();
        pr.meta.createdAt = pr.meta.updatedAt = nowMs();
        // New local projects default to a one-week expiration (mirrors the browser).
        pr.meta.expiresAt = core::ProjectsStore::addPeriod(
            pr.meta.updatedAt, core::ProjectsStore::DEFAULT_PERIOD);
        pr.meta.hasImage = true;
        pr.meta.source = meta.source.toStdString();
        pr.meta.resource = meta.resource.toStdString();
        pr.imagePath = path;
        int lw = 0, lh = 0;
        pr.lines = fileStore::parseLayoutJson(layout, lw, lh, &pr.cropRect, &pr.rotationQuarters);
        const QString newId = QString::fromStdString(pr.meta.id);
        projectList_->push_back(pr);
        fileStore::saveProjects(*projectList_);
        if (removeFromServer) {
          c->deleteProjectAsync(id, [this, c, newId, done](bool dok) {
            if (!dok)
              notify_->error(QString("Copied locally, but server delete failed — %1")
                                 .arg(c->lastError()));
            if (done) done(true, newId);
          });
        } else {
          if (done) done(true, newId);
        }
      };
      c->downloadFileAsync(id, "original", [this, meta, persist](bool dok, QByteArray bytes) {
        if (dok && !bytes.isEmpty()) {
          persist(bytes);
          return;
        }
        // No stored bytes (extension-added project records only a web URL) — fetch that directly.
        h_.fetchUrlBytes(meta.source, [persist](QByteArray b) { persist(b); });
      });
    });
  }

}  // namespace stencil::gui

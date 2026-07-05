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
  // layout (lines + filter + page/formulas) so the server holds the full project. Reports the
  // new id/version via out-params; false + notify on failure.
  bool ProjectTransferController::createServerFromLocal(stencil::net::ServerClient* c,
                                                        const Project& pr, const QString& name,
                                                        const QByteArray& bytes, const QString& ext,
                                                        int w, int h, QString& newIdOut,
                                                        qint64& newVersionOut) {
    QString newId;
    qint64 version = 0;
    if (!c->createProject(name, QString::fromStdString(pr.meta.source),
                          QString::fromStdString(pr.meta.resource), true, w, h, newId, version)) {
      notify_->error(QString("Could not create on server — %1").arg(c->lastError()));
      return false;
    }
    if (!c->uploadFile(newId, "original", bytes, ext, w, h)) {
      notify_->error(QString("Created, but image upload failed — %1").arg(c->lastError()));
      return false;
    }
    const QJsonObject layout = fileStore::buildLayoutJson(
        w, h, pr.lines, settings_->imageFilter, settings_->filterColor,
        pr.cropRect, pr.rotationQuarters, h_.currentLayoutMeta());
    qint64 newVersion = version;
    bool conflict = false;
    c->updateProject(newId, name, layout, version, newVersion, conflict);
    newIdOut = newId;
    newVersionOut = newVersion;
    return true;
  }

  // Local → server: create the project on `serverUrl`, upload its original image, push the
  // annotated layout, then drop the local copy. Mirrors the browser's moveProjectToServer().
  void ProjectTransferController::moveLocalProjectToServer(const QString& serverUrl,
                                                           const QString& id) {
    stencil::net::ConnectionManager* mgr = h_.connections();
    stencil::net::ServerClient* c = mgr ? mgr->find(serverUrl) : nullptr;
    if (!c) {
      notify_->error("Not connected to that server");
      return;
    }
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
    QString newId;
    qint64 newVersion = 0;
    if (!createServerFromLocal(c, *pr, name, bytes, ext, w, h, newId, newVersion)) return;
    if (!localColor.isEmpty()) {
      bool colorConflict = false;
      c->updateProjectColor(newId, localColor, newVersion, newVersion, colorConflict);
    }
    // The local copy is now redundant — remove it.
    const std::string sid = id.toStdString();
    const bool wasActive = (h_.activeProjectId() == id);
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
  }

  // Local → server COPY: create a new server project from a local one (default name
  // "<name>-copy"), leaving the local project in place. Mirrors browser copyProjectToServer.
  void ProjectTransferController::copyLocalProjectToServer(const QString& serverUrl,
                                                           const QString& id, const QString& name) {
    stencil::net::ConnectionManager* mgr = h_.connections();
    stencil::net::ServerClient* c = mgr ? mgr->find(serverUrl) : nullptr;
    if (!c) {
      notify_->error("Not connected to that server");
      return;
    }
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
    QString newId;
    qint64 newVersion = 0;
    if (!createServerFromLocal(c, *pr, copyName, bytes, ext, w, h, newId, newVersion)) return;
    h_.afterChange();
    notify_->success(QString("Copied \"%1\" to %2").arg(copyName, serverUrl));
  }

  // Server → local: download the project's image + layout, persist it as a new local project,
  // then delete it from the server. Mirrors moveProjectToLocal().
  void ProjectTransferController::moveServerProjectToLocal(const QString& serverUrl,
                                                           const QString& id) {
    // If this server project is the open remote session, follow it to local so the editor stays
    // open + focused instead of pointing at the deleted server id.
    const bool wasOpen = (h_.remoteId() == id && h_.remoteAddress() == serverUrl);
    QString newId;
    if (!importServerProjectToLocal(serverUrl, id, /*removeFromServer=*/true, "", &newId))
      return;
    if (wasOpen) h_.loadProjectIntoCanvas(newId);  // rebind the editor to the new local project
    h_.afterChange();
    notify_->success("Moved to local storage");
  }

  void ProjectTransferController::makeLocalCopyOfServerProject(const QString& serverUrl,
                                                              const QString& id,
                                                              const QString& name) {
    QString newId;
    if (!importServerProjectToLocal(serverUrl, id, /*removeFromServer=*/false, name, &newId))
      return;
    h_.afterChange();
    h_.loadProjectIntoCanvas(newId);  // open the detached copy (clears the remote link)
    notify_->success("Local copy created");
  }

  // Shared body: fetch a server project's image + layout (incl. crop/rotation), persist a fresh
  // detached local project; optionally delete the server copy. `name` (when non-empty) overrides
  // the server's name (used for the copy's "<name>-copy"). Errors are reported.
  bool ProjectTransferController::importServerProjectToLocal(const QString& serverUrl,
                                                             const QString& id, bool removeFromServer,
                                                             const QString& name, QString* newIdOut) {
    stencil::net::ConnectionManager* mgr = h_.connections();
    stencil::net::ServerClient* c = mgr ? mgr->find(serverUrl) : nullptr;
    if (!c) {
      notify_->error("Not connected to that server");
      return false;
    }
    stencil::net::ServerProject meta;
    QJsonObject layout;
    if (!c->getProject(id, meta, layout)) {
      notify_->error(QString("Could not fetch server project — %1").arg(c->lastError()));
      return false;
    }
    bool ok = false;
    QByteArray bytes = c->downloadFile(id, "original", ok);
    if (!ok || bytes.isEmpty()) bytes = h_.fetchUrlBytes(meta.source);  // extension-added: only a web URL
    if (bytes.isEmpty()) {
      notify_->error("Server project has no image");
      return false;
    }
    QImage img;
    if (!img.loadFromData(bytes)) {
      notify_->error("Server image could not be decoded");
      return false;
    }
    // Persist the bytes to a file under the state dir so the local project reloads its pixels on
    // open (local projects reference an on-disk imagePath).
    Project pr;
    pr.meta.id = store_->createId(nowMs(), makeSalt());
    const QString imgDir = fileStore::stateDir() + "/images";
    QDir().mkpath(imgDir);
    const QString path = imgDir + "/" + QString::fromStdString(pr.meta.id) + ".png";
    if (!img.save(path, "PNG")) {
      notify_->error("Could not write the image to local storage");
      return false;
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
    projectList_->push_back(pr);
    fileStore::saveProjects(*projectList_);
    if (removeFromServer && !c->deleteProject(id))
      notify_->error(QString("Copied locally, but server delete failed — %1").arg(c->lastError()));
    if (newIdOut) *newIdOut = QString::fromStdString(pr.meta.id);
    return true;
  }

}  // namespace stencil::gui

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

  // Local → server copy, default name "<name>-copy"; mirrors browser copyProjectToServer.
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

  // Mirrors moveProjectToLocal().
  void ProjectTransferController::moveServerProjectToLocal(const QString& serverUrl,
                                                           const QString& id) {
    // If this is the open remote session, follow it to local so the editor never points at the
    // deleted server id.
    const bool wasOpen = (h_.remoteId() == id && h_.remoteAddress() == serverUrl);
    importServerProjectToLocal(serverUrl, id, /*removeFromServer=*/true, "",
                               [this, wasOpen](bool ok, QString newId) {
      if (!ok) return;
      // A rebind, not an arrival.
      if (wasOpen) h_.loadProjectIntoCanvas(newId, /*animate=*/false);
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
      h_.loadProjectIntoCanvas(newId, /*animate=*/true);  // the detached copy OPENS (clears the remote link)
      notify_->success("Local copy created");
    });
  }

  // `name` overrides the server's; reports (ok, newLocalId). Async: getProject →
  // downloadFile("original") → fetchUrlBytes on empty → decode+persist → deleteProject.
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
        // Local projects reference an on-disk imagePath.
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
        // One-week default expiration (mirrors the browser).
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
        // No stored bytes (extension-added project): fetch the web URL.
        h_.fetchUrlBytes(meta.source, [persist](QByteArray b) { persist(b); });
      });
    });
  }
}  // namespace stencil::gui


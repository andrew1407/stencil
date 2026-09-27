#include "ProjectTransferController.hpp"
#include "projectTransferParts.hpp"
#include "../../support/rowWork.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "ServerClient.hpp"
#include <QDir>
#include <QImage>
#include <QJsonObject>
#include <algorithm>

namespace stencil::gui {

  // Local → server copy, default name "<name>-copy"; mirrors browser copyProjectToServer.
  void ProjectTransferController::copyLocalProjectToServer(const QString& serverUrl,
                                                           const QString& id, const QString& name) {
    if (!requireClient(serverUrl)) return;
    Project* found = this->h.findProject(id.toStdString());
    if (!found) {
      notify->error("Project not found");
      return;
    }
    localProjectOriginal(*found, [this, serverUrl, id, name](const Original& o) {
      stencil::net::ServerClient* c = requireClient(serverUrl);
      Project* pr = c ? this->h.findProject(id.toStdString()) : nullptr;
      if (!pr) return;   // disconnected, or removed while its picture was read
      const QString copyName = name.trimmed().isEmpty()
                                   ? (QString::fromStdString(pr->meta.name) + "-copy")
                                   : name.trimmed();
      createServerFromLocal(c, *pr, copyName, o.bytes, o.ext, o.w, o.h,
                            [this, copyName, serverUrl](bool ok, QString, qint64) {
        if (!ok) return;
        this->h.afterChange();
        notify->success(QString("Copied \"%1\" to %2").arg(copyName, serverUrl));
      });
    });
  }

  // Mirrors moveProjectToLocal().
  void ProjectTransferController::moveServerProjectToLocal(const QString& serverUrl,
                                                           const QString& id) {
    // If this is the open remote session, follow it to local so the editor never points at the
    // deleted server id.
    const bool wasOpen = (this->h.remoteId() == id && this->h.remoteAddress() == serverUrl);
    importServerProjectToLocal(serverUrl, id, /*removeFromServer=*/true, "",
                               [this, wasOpen](bool ok, QString newId) {
      if (!ok) return;
      const auto moved = [this] {
        this->h.afterChange();
        notify->success("Moved to local storage");
      };
      // A rebind, not an arrival.
      if (wasOpen) this->h.loadProjectIntoCanvas(newId, /*animate=*/false, moved);
      else moved();
    });
  }

  void ProjectTransferController::makeLocalCopyOfServerProject(const QString& serverUrl,
                                                              const QString& id,
                                                              const QString& name) {
    importServerProjectToLocal(serverUrl, id, /*removeFromServer=*/false, name,
                               [this](bool ok, QString newId) {
      if (!ok) return;
      this->h.afterChange();
      // The detached copy OPENS (clears the remote link).
      this->h.loadProjectIntoCanvas(newId, /*animate=*/true, [this] { notify->success("Local copy created"); });
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
        notify->error(QString("Could not fetch server project — %1").arg(c->lastError()));
        if (done) done(false, QString());
        return;
      }
      auto persist = [this, c, id, name, removeFromServer, meta, layout, done](QByteArray bytes) {
        if (bytes.isEmpty()) {
          notify->error("Server project has no image");
          if (done) done(false, QString());
          return;
        }
        // Local projects reference an on-disk imagePath; the decode and the PNG write run on the pool.
        const std::string newId = store->createId(nowMs(), makeSalt());
        const QString imgDir = fileStore::stateDir() + "/images";
        const QString path = imgDir + "/" + QString::fromStdString(newId) + ".png";
        enum class Stored { OK, UNDECODABLE, UNWRITABLE };
        support::runOnPool<Stored>(canvas, [bytes, imgDir, path] {
          QImage img;
          if (!img.loadFromData(bytes)) return Stored::UNDECODABLE;
          QDir().mkpath(imgDir);
          return img.save(path, "PNG") ? Stored::OK : Stored::UNWRITABLE;
        }, [this, c, id, name, removeFromServer, meta, layout, done, newId, path](Stored stored) {
          if (stored != Stored::OK) {
            notify->error(stored == Stored::UNDECODABLE ? "Server image could not be decoded"
                                                        : "Could not write the image to local storage");
            if (done) done(false, QString());
            return;
          }
          Project pr;
          pr.meta.id = newId;
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
          const QString localId = QString::fromStdString(newId);
          projectList->push_back(pr);
          fileStore::saveProjects(*projectList);
          if (removeFromServer) {
            c->deleteProjectAsync(id, [this, c, localId, done](bool dok) {
              if (!dok)
                notify->error(QString("Copied locally, but server delete failed — %1")
                                   .arg(c->lastError()));
              if (done) done(true, localId);
            });
          } else {
            if (done) done(true, localId);
          }
        });
      };
      c->downloadFileAsync(id, "original", [this, meta, persist](bool dok, QByteArray bytes) {
        if (dok && !bytes.isEmpty()) {
          persist(bytes);
          return;
        }
        // No stored bytes (extension-added project): fetch the web URL.
        this->h.fetchUrlBytes(meta.source, [persist](QByteArray b) { persist(b); });
      });
    });
  }
}  // namespace stencil::gui


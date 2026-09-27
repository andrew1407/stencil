#include "MainWindow.hpp"
#include "../../../support/rowWork.hpp"
#include "ProjectTitleController.hpp"
#include "mainWindowHelpers.hpp"
#include "IncognitoOverlay.hpp"
#include "ProjectFlows.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "RemoteSyncController.hpp"
#include "ServerClient.hpp"

// Publishing to a server: the incognito session, or a new server project (browser remoteSync.js).

namespace stencil::gui {

  // Mirrors the browser's publishIncognitoToServer (a server-backed project is not incognito).
  void ProjectFlows::publishIncognitoToServer(const QString& serverUrl) {
    if (!w.canvas->hasImage()) {
      w.notify->error("Open an image first");
      return;
    }
    if (w.incognito) {
      w.incognito = false;
      w.overlays.incognito->setActive(false);
      w.acts.incognito->blockSignals(true);
      w.acts.incognito->setChecked(false);
      w.acts.incognito->blockSignals(false);
    }
    QString name = w.canvas->imageBaseName();
    if (name.isEmpty()) name = QStringLiteral("Untitled");
    QPointer<MainWindow> self(&w);
    // Creation failure notifies and never fires onLinked, so nothing is pushed.
    createServerProject(serverUrl, name, [this, self]() {
      if (!self) return;
      // Pushed regardless of the sync toggle; saveToServer reads settings.syncToServer only at
      // entry, so restoring it right after is safe.
      const bool savedSync = w.settings.syncToServer;
      w.settings.syncToServer = true;
      saveToServer();
      w.settings.syncToServer = savedSync;
      w.remoteSync->startRemotePoll();   // live co-edit: watch for peers changing this project
      w.refreshActions();
      w.projectTitle->updateProjectTitle();
    });
  }

  // POST /projects, upload the 'original', link the session. Mirrors the browser's
  // createRemoteProject (remoteSync.js).
  void ProjectFlows::createServerProject(const QString& serverUrl, const QString& name,
                                       std::function<void()> onLinked) {
    stencil::net::ServerClient* c = w.remote.session->requireClient(serverUrl);
    if (!c) return;
    const bool hasImage = w.canvas->hasImage();
    const int width = hasImage ? w.canvas->imageWidth() : 0;
    const int height = hasImage ? w.canvas->imageHeight() : 0;
    QPointer<MainWindow> self(&w);
    c->createProjectAsync(
        name, w.docSource.currentSource, w.docSource.currentResource, hasImage, width, height,
        [this, self, c, serverUrl, name, hasImage, width, height, onLinked](bool ok, QString id,
                                                                   qint64 version) {
          if (!self) return;
          if (!ok) {
            w.notify->error(QString("Could not create on server — %1").arg(c->lastError()));
            return;
          }
          // A fresh server project has no custom colour; `onLinked` fires only on success.
          auto link = [this, self, serverUrl, id, name, onLinked](qint64 v) {
            if (!self) return;
            w.activeProjectId.clear();
            w.remote.session->getLink().bind(serverUrl, id, name, QString(), v);
            w.refreshActions();
            w.notify->success(QString("Created \"%1\" on %2").arg(name, serverUrl));
            if (onLinked) onLinked();
          };
          if (!hasImage) {
            link(version);
            return;
          }
          // The image is shared, not copied: the full-size PNG encode runs on the pool.
          const QImage img = w.canvas->getImage();
          support::runOnPool<QByteArray>(&w, [img] { return pngBytes(img); },
              [this, self, serverUrl, id, width, height, version, link](QByteArray bytes) {
            stencil::net::ServerClient* c = w.remote.connections ? w.remote.connections->find(serverUrl) : nullptr;
            if (!c) {
              w.notify->error("Created, but image upload failed — not connected");
              link(version);
              return;
            }
            c->uploadFileAsync(id, "original", bytes, "png", width, height,
                               [this, self, c, id, version, link](bool uok) {
                                 if (!self) return;
                                 if (!uok) {
                                   w.notify->error(QString("Created, but image upload failed — %1")
                                                      .arg(c->lastError()));
                                   // Still link below so the user can retry via Save.
                                   link(version);
                                   return;
                                 }
                                 // The file write bumps the version; re-read it for the next guard
                                 // (mirrors remoteSync.currentVersion()).
                                 c->getProjectAsync(id, [self, version, link](
                                                            bool gok, stencil::net::ServerProject meta,
                                                            QJsonObject) {
                                   if (!self) return;
                                   link(gok ? meta.version : version);
                                 });
                               });
          });
        });
  }

}  // namespace stencil::gui

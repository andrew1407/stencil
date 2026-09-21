#include "MainWindow.hpp"
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "ProjectTransferController.hpp"
#include "ServerClient.hpp"

#include <QJsonObject>

// Adopting the canvas as a project, and creating one on a server.

namespace stencil::gui {

  void MainWindow::adoptCanvasAsLocalProject() {
    // Incognito never persists; a server session owns its saving; an active project is
    // open/replace, not a fresh load.
    const QString serverTarget = pendingServerTarget;   // consumed either way
    pendingServerTarget.clear();
    if (incognito) return;
    if (!activeProjectId.isEmpty() || !remoteSession->getLink().address.isEmpty()) return;
    if (!canvas->hasImage()) return;
    // Mirrors newProjectFromCanvas.
    QString seed = canvas->imageBaseName();
    if (seed.isEmpty()) {
      std::vector<core::ProjectMeta> metas;
      for (const auto& pr : projectList) metas.push_back(pr.meta);
      core::ProjectsStore tmp;
      tmp.load(metas);
      seed = QString::fromStdString(tmp.defaultName());
    }
    // The Open dialog's "Save to" pick (browser openImageModal.js `address`).
    if (!serverTarget.isEmpty()) {
      createServerProject(serverTarget, seed);
      return;
    }
    createLocalProject(seed, /*announce=*/false);  // the load path already notified
    // Browser parity: storage.save() flashes "Saved".
    notify->success(QStringLiteral("Saved"));
  }

  // POST /projects, upload the 'original', link the session. Mirrors the browser's
  // createRemoteProject (remoteSync.js).
  void MainWindow::createServerProject(const QString& serverUrl, const QString& name,
                                       std::function<void()> onLinked) {
    stencil::net::ServerClient* c = remoteSession->requireClient(serverUrl);
    if (!c) return;
    const bool hasImage = canvas->hasImage();
    const int w = hasImage ? canvas->imageWidth() : 0;
    const int h = hasImage ? canvas->imageHeight() : 0;
    QPointer<MainWindow> self(this);
    c->createProjectAsync(
        name, currentSource, currentResource, hasImage, w, h,
        [this, self, c, serverUrl, name, hasImage, w, h, onLinked](bool ok, QString id,
                                                                   qint64 version) {
          if (!self) return;
          if (!ok) {
            notify->error(QString("Could not create on server — %1").arg(c->lastError()));
            return;
          }
          // A fresh server project has no custom colour; `onLinked` fires only on success.
          auto link = [this, self, serverUrl, id, name, onLinked](qint64 v) {
            if (!self) return;
            activeProjectId.clear();
            remoteSession->getLink().bind(serverUrl, id, name, QString(), v);
            refreshActions();
            notify->success(QString("Created \"%1\" on %2").arg(name, serverUrl));
            if (onLinked) onLinked();
          };
          if (!hasImage) {
            link(version);
            return;
          }
          const QByteArray bytes = pngBytes(canvas->getImage());
          c->uploadFileAsync(id, "original", bytes, "png", w, h,
                             [this, self, c, id, version, link](bool uok) {
                               if (!self) return;
                               if (!uok) {
                                 notify->error(QString("Created, but image upload failed — %1")
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
  }

}  // namespace stencil::gui

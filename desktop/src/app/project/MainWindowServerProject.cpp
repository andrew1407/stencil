#include "MainWindow.hpp"
#include "mainWindowShared.hpp"
#include "MainWindow.hpp"
#include "ChatPlanTarget.hpp"
#include "displayName.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "LinksDialog.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "RemoteSyncController.hpp"
#include "ProjectTransferController.hpp"
#include "ServerClient.hpp"

#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>

// Opening a server-stored project.

namespace stencil::gui {

  // Mirrors the browser projectsModal openRemote(). Async chain: getProject →
  // downloadFile("original") → fetchUrlBytes(source) on empty → decode → adopt.
  void MainWindow::openServerProject(const QString& serverUrl, const QString& id, bool silent,
                                     bool link) {
    if (!connections) return;
    stencil::net::ServerClient* c = remoteSession->requireClient(serverUrl);
    if (!c) return;
    // Loading emits changed(); the flag stops it being pushed straight back. Async-in-flight: the
    // shared clearer's destructor resets it once the last continuation is gone.
    remoteReloading = true;
    auto reloadGuard = std::shared_ptr<void>(nullptr, [self = QPointer<MainWindow>(this)](void*) {
      if (self) self->remoteReloading = false;
    });
    QPointer<MainWindow> self(this);
    c->getProjectAsync(id, [this, self, c, serverUrl, id, silent, link, reloadGuard](
                               bool ok, stencil::net::ServerProject meta, QJsonObject layout) {
      if (!self) return;
      if (!ok) {
        notify->error(QString("Could not open server project — %1").arg(c->lastError()));
        return;
      }
      auto adopt = [this, self, serverUrl, id, silent, link, meta, layout,
                    reloadGuard](QByteArray bytes) {
        if (!self) return;
        QImage img;
        if (!img.loadFromData(bytes)) {
          notify->error("Server image could not be decoded");
          return;
        }
        loadImageWithLayout(img, layout);
        blankColor = meta.blankColor;  // restore blank-fill so the recolour control tracks it
        canvas->setBlankPage(!blankColor.isEmpty());
        // Unlinked (incognito deep-link) opens adopt the content only, mirroring the browser's
        // copyServerProjectToIncognito.
        activeProjectId.clear();
        if (link) {
          remoteSession->getLink().bind(serverUrl, id, meta.name, meta.color, meta.version);
        } else {
          remoteSession->getLink().unbind();
          remoteSync->stopRemotePoll();
        }
        currentSource = meta.source;
        currentResource = meta.resource;
        filterDirty = false;   // we just adopted the server/project filter
        refreshActions();
        // Fit on open (browser switchToProject); skipped for a silent live-poll reload, dust
        // arrival included — a peer's stroke is an edit, not an image appearing.
        if (!silent) {
          fitToWindow();
          playImageArrival();
        }
        if (link) remoteSync->startRemotePoll();   // live co-edit: watch for peer changes
        // Chat persistence (§12): a linked, user-initiated open pulls the server-stored chat;
        // silent reloads must not stomp it.
        if (link && !silent && settings.saveChatsWithProject) {
          if (auto* cc = connections ? connections->find(serverUrl) : nullptr) {
            cc->downloadFileAsync(id, QStringLiteral("chat"),
                                  [this, self](bool cok, QByteArray data) {
                                    if (!self) return;
                                    restoreChatFromDoc(
                                        cok ? QJsonDocument::fromJson(data).object() : QJsonObject());
                                  });
          } else {
            restoreChatFromDoc(QJsonObject());
          }
        }
        if (!silent)
          notify->success(QString("Opened \"%1\" from %2")
                               .arg(support::shortName(meta.name.isEmpty() ? QStringLiteral("Untitled") : meta.name),
                                    serverUrl));
      };
      c->downloadFileAsync(id, "original", [this, self, c, meta, adopt,
                                            reloadGuard](bool dok, QByteArray bytes) {
        if (!self) return;
        if (dok && !bytes.isEmpty()) {
          adopt(bytes);
          return;
        }
        // No stored bytes (an extension-added project records only the web URL): fetch that
        // source. Qt Network has no CORS limit.
        fetchUrlBytesAsync(this, meta.source, [this, self, c, adopt, reloadGuard](QByteArray b) {
          if (!self) return;
          if (b.isEmpty()) {
            notify->error(QString("Could not download image — %1").arg(c->lastError()));
            return;
          }
          adopt(b);
        });
      });
    });
  }

}  // namespace stencil::gui

#include "mainWindow.hpp"
#include "mainWindowShared.hpp"
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "displayName.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "linksDialog.hpp"
#include "notifications.hpp"
#include "remoteSession.hpp"
#include "remoteSyncController.hpp"
#include "projectTransferController.hpp"
#include "serverClient.hpp"

#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>

// Opening a server-stored project.

namespace stencil::gui {

  // Open a server-stored project: fetch its record (name/version/layout) + the
  // original image bytes, load them onto this canvas, and link the session so a
  // later Save writes back. Mirrors the browser projectsModal openRemote(). Async: chains
  // getProject → downloadFile("original") → (on empty) fetchUrlBytes(source) → decode → adopt.
  void MainWindow::openServerProject(const QString& serverUrl, const QString& id, bool silent,
                                     bool link) {
    if (!connections_) return;
    stencil::net::ServerClient* c = remoteSession_->requireClient(serverUrl);
    if (!c) return;
    // Loading the canvas below emits changed() — guard so it isn't taken for a user edit and
    // pushed straight back (feedback loop). This flag now reflects async-in-flight state (there is
    // no nested loop): set true here, cleared automatically when the whole chain ends. The shared
    // clearer's destructor runs once the last pending continuation is gone (success, error, OR the
    // client/window destroyed mid-flight), so the flag can never stick true.
    remoteReloading_ = true;
    auto reloadGuard = std::shared_ptr<void>(nullptr, [self = QPointer<MainWindow>(this)](void*) {
      if (self) self->remoteReloading_ = false;
    });
    QPointer<MainWindow> self(this);
    c->getProjectAsync(id, [this, self, c, serverUrl, id, silent, link, reloadGuard](
                               bool ok, stencil::net::ServerProject meta, QJsonObject layout) {
      if (!self) return;
      if (!ok) {
        notify_->error(QString("Could not open server project — %1").arg(c->lastError()));
        return;
      }
      // Adopt the decoded bytes onto the canvas + link the session (the tail shared by the
      // stored-bytes and source-URL-fallback paths).
      auto adopt = [this, self, serverUrl, id, silent, link, meta, layout,
                    reloadGuard](QByteArray bytes) {
        if (!self) return;
        QImage img;
        if (!img.loadFromData(bytes)) {
          notify_->error("Server image could not be decoded");
          return;
        }
        // Adopt the full layout (page/formulas + geometry + lines + filter) onto the image.
        loadImageWithLayout(img, layout);
        blankColor_ = meta.blankColor;  // restore blank-fill so the recolour control tracks it
        canvas_->setBlankPage(!blankColor_.isEmpty());
        // Link the session; clear any local-project linkage so saves go to the server.
        // Unlinked (incognito deep-link) opens adopt the content only: no remote link,
        // no live co-edit, nothing ever pushed back — mirroring the browser's
        // copyServerProjectToIncognito semantics.
        activeProjectId_.clear();
        if (link) {
          remoteSession_->link().bind(serverUrl, id, meta.name, meta.color, meta.version);
        } else {
          remoteSession_->link().unbind();
          remoteSync_->stopRemotePoll();
        }
        currentSource_ = meta.source;
        currentResource_ = meta.resource;
        filterDirty_ = false;   // we just adopted the server/project filter
        refreshActions();
        // Fit the freshly-opened image to the window (matches the browser's switchToProject).
        // Skipped for a silent live-poll reload so a peer's edit doesn't reset zoom/pan —
        // which is also why the dust arrival is skipped there: a peer's stroke landing is
        // an edit, not an image appearing.
        if (!silent) {
          fitToWindow();
          playImageArrival();
        }
        if (link) remoteSync_->startRemotePoll();   // live co-edit: watch for peer changes
        // Chat persistence (§12): a linked, user-initiated open pulls the
        // project's server-stored chat (silent live-poll reloads must not stomp
        // the conversation mid-thought). Missing/invalid = a fresh scope.
        if (link && !silent && settings_.saveChatsWithProject) {
          if (auto* cc = connections_ ? connections_->find(serverUrl) : nullptr) {
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
          notify_->success(QString("Opened \"%1\" from %2")
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
        // No stored bytes on the server (e.g. an extension-added project that only recorded the
        // image's web URL) — fetch that source URL directly. Qt Network has no CORS limit.
        fetchUrlBytesAsync(this, meta.source, [this, self, c, adopt, reloadGuard](QByteArray b) {
          if (!self) return;
          if (b.isEmpty()) {
            notify_->error(QString("Could not download image — %1").arg(c->lastError()));
            return;
          }
          adopt(b);
        });
      });
    });
  }

}  // namespace stencil::gui

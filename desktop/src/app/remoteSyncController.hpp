#pragma once
#include <QObject>
#include <QString>
#include <functional>

class QTimer;

namespace stencil::net {
  class ConnectionManager;
  class LiveFeed;
}

namespace stencil::gui {

  class RemoteSession;

  // ── RemoteSyncController: live co-edit push/pull engine ─────────────────────
  // Extracted from MainWindow. A QObject that owns the three sync timers (debounced push,
  // backstop poll, coalesced reload) and the LiveFeed subscription, mirroring the browser's
  // remoteSyncController.js. It composes the RemoteSession DIRECTLY — reading the remote-link
  // state (address/id/version) and the ConnectionManager through it rather than through function
  // hooks. Only the bits that still live on MainWindow are hooks: the shared reentrancy flags
  // (two const bool*, now reflecting async-in-flight state), the syncToServer/incognito predicates,
  // and the two canvas-driving actions (saveToServer / openServerProject, both async). The
  // sync-internal timing (push-burst start, reload-pending) lives here.
  class RemoteSyncController : public QObject {
    Q_OBJECT
   public:
    struct Hooks {
      std::function<bool()> syncToServer;
      std::function<bool()> incognito;
      std::function<void()> saveToServer;
      std::function<void(const QString& addr, const QString& id, bool silent)> openServerProject;
    };

    RemoteSyncController(QObject* parent, RemoteSession* session, const bool* remoteReloading,
                         const bool* remotePushing, Hooks hooks);

    // Debounced save-back of local edits (called on every edit that rides the layout).
    void scheduleRemotePush();
    // Start/stop watching the linked project for peer changes (poll backstop + live feed).
    void startRemotePoll();
    void stopRemotePoll();

   private:
    void ensureLiveFeed();
    void onRemoteProjectEvent(const QString& id, qint64 version, bool deleted);
    void pollRemoteForUpdate();

    QTimer* pushTimer_;
    QTimer* pollTimer_;
    QTimer* reloadTimer_;
    stencil::net::LiveFeed* liveFeed_ = nullptr;
    qint64 pushBurstStart_ = 0;   // start of the current debounce burst (max-wait cap)
    bool reloadPending_ = false;  // a peer change queued during a reload's nested loop
    RemoteSession* session_;       // the server-project session (link state + connections)
    const bool* remoteReloading_;  // owned by MainWindow (true while an async reload is in flight)
    const bool* remotePushing_;    // owned by MainWindow (true while an async push is in flight)
    Hooks h_;
  };

}  // namespace stencil::gui

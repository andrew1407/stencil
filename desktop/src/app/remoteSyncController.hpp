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

  // ── RemoteSyncController: live co-edit push/pull engine ─────────────────────
  // Extracted from MainWindow. A QObject that owns the three sync timers (debounced push,
  // backstop poll, coalesced reload) and the LiveFeed subscription, mirroring the browser's
  // remoteSyncController.js. It holds NO MainWindow back-pointer: the session's remote-link
  // state (address/id/version) + the shared reentrancy flags stay on MainWindow and are read
  // through the Hooks callbacks + two const bool* flags; the two actions that spin nested event
  // loops (saveToServer / openServerProject) are also hooks. Only the sync-internal timing
  // (push-burst start, reload-pending) lives here.
  class RemoteSyncController : public QObject {
    Q_OBJECT
   public:
    struct Hooks {
      std::function<stencil::net::ConnectionManager*()> connections;
      std::function<QString()> remoteAddress;
      std::function<QString()> remoteId;
      std::function<qint64()> remoteVersion;
      std::function<bool()> syncToServer;
      std::function<bool()> incognito;
      std::function<void()> saveToServer;
      std::function<void(const QString& addr, const QString& id, bool silent)> openServerProject;
    };

    RemoteSyncController(QObject* parent, const bool* remoteReloading,
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
    const bool* remoteReloading_;  // owned by MainWindow (ScopedFlag during a reload)
    const bool* remotePushing_;    // owned by MainWindow (ScopedFlag during a push)
    Hooks h_;
  };

}  // namespace stencil::gui

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

  // Live co-edit engine: the three sync timers + the LiveFeed subscription, mirroring the
  // browser's remoteSyncController.js. Composes RemoteSession directly; the canvas-driving actions
  // stay on MainWindow as hooks.
  class RemoteSyncController : public QObject {
    Q_OBJECT
   public:
    struct Hooks {
      std::function<bool()> syncToServer;
      std::function<bool()> incognito;
      std::function<void()> saveToServer;
      std::function<void(const QString& addr, const QString& id, bool silent)> openServerProject;
      std::function<void()> serverProjectDeleted;
    };

    RemoteSyncController(QObject* parent, RemoteSession* session, const bool* remoteReloading,
                         const bool* remotePushing, Hooks hooks);

    void scheduleRemotePush();
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

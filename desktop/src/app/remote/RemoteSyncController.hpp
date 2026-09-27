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

  // Live co-edit engine: the sync timers + the LiveFeed subscription, mirroring the browser's
  // remote/syncController.js. The canvas-driving actions stay on MainWindow as hooks.
  class RemoteSyncController : public QObject {
    Q_OBJECT
   public:
    struct Hooks {
      std::function<bool()> syncToServer;
      std::function<bool()> incognito;
      std::function<void()> saveToServer;
      std::function<void(const QString& addr, const QString& id, bool silent)> openServerProject;
      std::function<void()> serverProjectDeleted;
      // Renders and uploads the baked result; `done` runs once it has landed or failed.
      std::function<void(std::function<void()> done)> uploadResult;
    };

    RemoteSyncController(QObject* parent, RemoteSession* session, const bool* remoteReloading,
                         const bool* remotePushing, const bool* planRunning, Hooks hooks);

    void scheduleRemotePush();
    void startRemotePoll();
    void stopRemotePoll();

    // The peer-change entry: a LiveFeed event, or a version the poll found ahead of ours.
    void onRemoteProjectEvent(const QString& id, qint64 version, bool deleted);

    // A committed layout push leaves the result stale; it is re-rendered once edits idle, never
    // twice within the gap. Flush starts it now; the bool says whether anything was left to send.
    void scheduleResultUpload();
    bool flushResultUpload(std::function<void()> settled = {});
    bool resultBusy() const { return resultDirty || resultInFlight; }
    // A closing window waits for its last result, capped; true once, when it must close again.
    bool holdCloseForResult(std::function<void()> reclose);
    void setResultTiming(int idleMs, int gapMs) { resultIdleMs = idleMs; resultGapMs = gapMs; }

    // ms, constants.json COEDIT.resultIdleMs / resultMinGapMs and POLL.remoteMs, read once; the
    // constants are what a build without the qrc uses, held equal to the table by the suite.
    static int tableMs(const char* section, const char* key, int fallback);
    static constexpr int RESULT_IDLE_MS = 2000;
    static constexpr int RESULT_GAP_MS = 10000;
    static constexpr int POLL_MS = 2000;
    static constexpr int RESULT_CLOSE_CAP_MS = 5000;

   private:
    void ensureLiveFeed();
    void pollRemoteForUpdate();
    void armResultTimer();
    void startResultUpload();
    bool localWriteBusy() const;

    QTimer* pushTimer;
    QTimer* pollTimer;
    QTimer* reloadTimer;
    QTimer* resultTimer;
    stencil::net::LiveFeed* liveFeed = nullptr;
    qint64 pushBurstStart = 0;   // start of the current debounce burst (max-wait cap)
    bool reloadPending = false;  // a peer change queued during a reload's nested loop
    qint64 reloadVersion = 0;    // the newest peer version queued; our own echo never reloads
    bool resultDirty = false;
    bool resultInFlight = false;
    qint64 resultDirtySince = 0;   // ms epoch; caps the idle wait under continuous editing
    qint64 lastResultAt = 0;
    int resultIdleMs = tableMs("COEDIT", "resultIdleMs", RESULT_IDLE_MS);
    int resultGapMs = tableMs("COEDIT", "resultMinGapMs", RESULT_GAP_MS);
    std::function<void()> resultSettled;
    bool closeHeld = false;
    RemoteSession* session;       // the server-project session (link state + connections)
    const bool* remoteReloading;  // owned by MainWindow (true while an async reload is in flight)
    const bool* remotePushing;    // owned by MainWindow (true while an async push is in flight)
    const bool* planRunning;      // owned by ChatSessionController (true while an op plan holds the canvas)
    Hooks h;
  };

}  // namespace stencil::gui

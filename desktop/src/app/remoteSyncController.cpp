#include "remoteSyncController.hpp"
#include "liveFeed.hpp"
#include "remoteSession.hpp"
#include "serverClient.hpp"
#include <QDateTime>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <algorithm>

namespace stencil::gui {

  RemoteSyncController::RemoteSyncController(QObject* parent, RemoteSession* session,
                                            const bool* remoteReloading,
                                            const bool* remotePushing, Hooks hooks)
      : QObject(parent), session_(session), remoteReloading_(remoteReloading),
        remotePushing_(remotePushing), h_(std::move(hooks)) {
    // Live co-edit: debounced push of local edits + periodic poll for peer changes.
    pushTimer_ = new QTimer(this);
    pushTimer_->setSingleShot(true);
    connect(pushTimer_, &QTimer::timeout, this, [this] {
      pushBurstStart_ = 0;   // burst flushed — start a fresh max-wait window next edit
      if (!session_->address().isEmpty()) h_.saveToServer();
    });
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(2000);   // backstop behind the live push feed
    connect(pollTimer_, &QTimer::timeout, this, [this] { pollRemoteForUpdate(); });
    // Coalesce a burst of live-feed events into one reload, and run it off the socket read slot
    // (openServerProject is async, so firing from a timer keeps it off the read slot and lets a
    // burst coalesce). Re-checked against the remote version at fire time.
    reloadTimer_ = new QTimer(this);
    reloadTimer_->setSingleShot(true);
    connect(reloadTimer_, &QTimer::timeout, this, [this] {
      if (session_->address().isEmpty() || session_->id().isEmpty()) return;
      if (!h_.syncToServer()) return;
      // A reload is still in flight (async): wait it out, then converge. openServerProject holds
      // remoteReloading_ true for its whole async lifetime, so we can't run a second reload on top;
      // keep the pending flag and re-poll shortly — when the flag clears this timer reloads the
      // latest. (Replaces the old synchronous "re-check reloadPending_ after the nested loop" tail.)
      if (*remoteReloading_) {
        if (reloadPending_) reloadTimer_->start(50);
        return;
      }
      // A local edit is pending/in-flight — don't clobber it; retry shortly (our push wins
      // last-writer-wins, then we reload the merged result).
      if (*remotePushing_ || (pushTimer_ && pushTimer_->isActive())) {
        reloadPending_ = true;
        reloadTimer_->start(150);
        return;
      }
      if (!reloadPending_) return;   // nothing queued → nothing to do
      reloadPending_ = false;
      // Async reload of the linked project. An event that lands while it's in flight re-arms this
      // timer (onRemoteProjectEvent) or trips the in-flight branch above, so we converge afterward.
      h_.openServerProject(session_->address(), session_->id(), /*silent=*/true);
    });
  }

  void RemoteSyncController::scheduleRemotePush() {
    // Sync off → a fetched project is edit-in-memory only: never auto-push to peers.
    if (h_.incognito() || *remoteReloading_ || session_->address().isEmpty() || !h_.syncToServer()) return;
    // Trailing debounce (coalesce a burst of edits into one save) capped by a max-wait, so
    // continuous editing still flushes to peers every ~1.5s instead of starving until a pause.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (pushBurstStart_ == 0) pushBurstStart_ = now;
    const int wait = std::clamp<int>(1500 - static_cast<int>(now - pushBurstStart_), 0, 350);
    pushTimer_->start(wait);
  }

  void RemoteSyncController::startRemotePoll() {
    if (pollTimer_ && !session_->address().isEmpty()) pollTimer_->start();
    // Subscribe the live push feed so peer edits arrive in tens of ms; the poll above is
    // now just a backstop (https servers / a dropped socket).
    ensureLiveFeed();
  }

  void RemoteSyncController::stopRemotePoll() {
    if (pollTimer_) pollTimer_->stop();
    if (reloadTimer_) reloadTimer_->stop();
    if (liveFeed_) liveFeed_->unsubscribe();
  }

  // Lazily build the push feed and (re)point it at the active server, authenticating with that
  // connection's token. A no-op when the session isn't server-linked or the server isn't
  // connected. subscribe() is idempotent for the same origin (token refresh only).
  void RemoteSyncController::ensureLiveFeed() {
    const QString addr = session_->address();
    if (addr.isEmpty()) return;
    stencil::net::ConnectionManager* mgr = session_->connections();
    stencil::net::ServerClient* c = mgr ? mgr->find(addr) : nullptr;
    if (!c) return;
    if (!liveFeed_) {
      liveFeed_ = new stencil::net::LiveFeed(this);
      connect(liveFeed_, &stencil::net::LiveFeed::projectUpdated,
              this, &RemoteSyncController::onRemoteProjectEvent);
    }
    liveFeed_->subscribe(addr, c->token());
  }

  // A live-feed push frame arrived. Reload (debounced) when it's a genuine peer change to the
  // project we're editing — newer version, not our own echo, not mid-push. Mirrors the browser's
  // onServerProjectEvent + shouldReloadFromEvent guards; the actual reload runs from the reload
  // timer so it lands off this slot and coalesces a burst.
  void RemoteSyncController::onRemoteProjectEvent(const QString& id, qint64 version, bool deleted) {
    if (session_->address().isEmpty() || session_->id().isEmpty()) return;
    if (id != session_->id() || deleted) return;
    if (!h_.syncToServer()) return;
    if (version <= session_->version()) return;  // our own save echo, or stale
    // Queue a reload and (re)arm the coalescing timer. Setting reloadPending_ here (rather than
    // only in the timer) means an event arriving while an async reload is in flight is remembered:
    // the timer's in-flight branch keeps polling until the reload clears, then converges to the
    // latest. The timer slot re-checks the reload/push guards at fire time.
    reloadPending_ = true;
    if (reloadTimer_) reloadTimer_->start(40);
  }

  // One poll tick: if a peer bumped the linked project's version, reload the canvas. Skipped
  // while a local edit is pending/in-flight so we never clobber the user's work or reload our
  // own change.
  void RemoteSyncController::pollRemoteForUpdate() {
    const QString addr = session_->address();
    const QString id = session_->id();
    if (addr.isEmpty() || id.isEmpty()) return;
    if (!h_.syncToServer()) return;  // sync off — don't pull peer changes over local edits
    if (*remotePushing_ || (pushTimer_ && pushTimer_->isActive())) return;
    stencil::net::ConnectionManager* mgr = session_->connections();
    stencil::net::ServerClient* c = mgr ? mgr->find(addr) : nullptr;
    if (!c) return;
    QPointer<RemoteSyncController> self(this);
    c->getProjectAsync(id, [this, self, addr, id](bool ok, stencil::net::ServerProject meta,
                                                  QJsonObject) {
      if (!self || !ok) return;
      // Re-check at completion time: the session may have changed, or a local push may have
      // started while the GET was in flight (don't clobber the user's work with a stale pull).
      if (session_->address() != addr || session_->id() != id) return;
      if (*remotePushing_ || (pushTimer_ && pushTimer_->isActive())) return;
      if (meta.version > session_->version())
        h_.openServerProject(addr, id, /*silent=*/true);
    });
  }

}  // namespace stencil::gui

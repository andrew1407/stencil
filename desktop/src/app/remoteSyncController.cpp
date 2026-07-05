#include "remoteSyncController.hpp"
#include "liveFeed.hpp"
#include "serverClient.hpp"
#include <QDateTime>
#include <QJsonObject>
#include <QTimer>
#include <algorithm>

namespace stencil::gui {

  RemoteSyncController::RemoteSyncController(QObject* parent, const bool* remoteReloading,
                                            const bool* remotePushing, Hooks hooks)
      : QObject(parent), remoteReloading_(remoteReloading),
        remotePushing_(remotePushing), h_(std::move(hooks)) {
    // Live co-edit: debounced push of local edits + periodic poll for peer changes.
    pushTimer_ = new QTimer(this);
    pushTimer_->setSingleShot(true);
    connect(pushTimer_, &QTimer::timeout, this, [this] {
      pushBurstStart_ = 0;   // burst flushed — start a fresh max-wait window next edit
      if (!h_.remoteAddress().isEmpty()) h_.saveToServer();
    });
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(2000);   // backstop behind the live push feed
    connect(pollTimer_, &QTimer::timeout, this, [this] { pollRemoteForUpdate(); });
    // Coalesce a burst of live-feed events into one reload, and run it off the socket read slot
    // (openServerProject spins a nested event loop, so a direct call would re-enter). Re-checked
    // against the remote version at fire time.
    reloadTimer_ = new QTimer(this);
    reloadTimer_->setSingleShot(true);
    connect(reloadTimer_, &QTimer::timeout, this, [this] {
      if (h_.remoteAddress().isEmpty() || h_.remoteId().isEmpty()) return;
      if (!h_.syncToServer()) return;
      if (*remoteReloading_) { reloadPending_ = true; return; }  // nested-loop guard
      // A local edit is pending/in-flight — don't clobber it; retry shortly (our push wins
      // last-writer-wins, then we reload the merged result).
      if (*remotePushing_ || (pushTimer_ && pushTimer_->isActive())) {
        reloadPending_ = true;
        reloadTimer_->start(150);
        return;
      }
      reloadPending_ = false;
      h_.openServerProject(h_.remoteAddress(), h_.remoteId(), /*silent=*/true);
      // Events that landed during the reload's nested event loop queued a pending flag —
      // converge to the latest with one more pass.
      if (reloadPending_) {
        reloadPending_ = false;
        reloadTimer_->start(40);
      }
    });
  }

  void RemoteSyncController::scheduleRemotePush() {
    // Sync off → a fetched project is edit-in-memory only: never auto-push to peers.
    if (h_.incognito() || *remoteReloading_ || h_.remoteAddress().isEmpty() || !h_.syncToServer()) return;
    // Trailing debounce (coalesce a burst of edits into one save) capped by a max-wait, so
    // continuous editing still flushes to peers every ~1.5s instead of starving until a pause.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (pushBurstStart_ == 0) pushBurstStart_ = now;
    const int wait = std::clamp<int>(1500 - static_cast<int>(now - pushBurstStart_), 0, 350);
    pushTimer_->start(wait);
  }

  void RemoteSyncController::startRemotePoll() {
    if (pollTimer_ && !h_.remoteAddress().isEmpty()) pollTimer_->start();
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
    const QString addr = h_.remoteAddress();
    if (addr.isEmpty()) return;
    stencil::net::ConnectionManager* mgr = h_.connections();
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
    if (h_.remoteAddress().isEmpty() || h_.remoteId().isEmpty()) return;
    if (id != h_.remoteId() || deleted) return;
    if (!h_.syncToServer()) return;
    if (version <= h_.remoteVersion()) return;  // our own save echo, or stale
    // A reload is mid-flight (its nested loop is pumping this slot): queue one more pass rather
    // than dropping the change — the reload's tail re-arms from reloadPending_.
    if (*remoteReloading_) { reloadPending_ = true; return; }
    // Coalesce a burst of peer events into a single debounced reload. The timer slot re-checks
    // the push guards at fire time (state may change within the window).
    if (reloadTimer_) reloadTimer_->start(40);
  }

  // One poll tick: if a peer bumped the linked project's version, reload the canvas. Skipped
  // while a local edit is pending/in-flight so we never clobber the user's work or reload our
  // own change.
  void RemoteSyncController::pollRemoteForUpdate() {
    const QString addr = h_.remoteAddress();
    const QString id = h_.remoteId();
    if (addr.isEmpty() || id.isEmpty()) return;
    if (!h_.syncToServer()) return;  // sync off — don't pull peer changes over local edits
    if (*remotePushing_ || (pushTimer_ && pushTimer_->isActive())) return;
    stencil::net::ConnectionManager* mgr = h_.connections();
    stencil::net::ServerClient* c = mgr ? mgr->find(addr) : nullptr;
    if (!c) return;
    stencil::net::ServerProject meta;
    QJsonObject layout;
    if (!c->getProject(id, meta, layout)) return;
    if (meta.version > h_.remoteVersion())
      h_.openServerProject(addr, id, /*silent=*/true);
  }

}  // namespace stencil::gui

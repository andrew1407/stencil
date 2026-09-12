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
    pushTimer_ = new QTimer(this);
    pushTimer_->setSingleShot(true);
    connect(pushTimer_, &QTimer::timeout, this, [this] {
      pushBurstStart_ = 0;   // burst flushed — start a fresh max-wait window next edit
      if (!session_->address().isEmpty()) h_.saveToServer();
    });
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(2000);   // backstop behind the live push feed
    connect(pollTimer_, &QTimer::timeout, this, [this] { pollRemoteForUpdate(); });
    // Coalesce a burst of live-feed events into one reload, off the socket read slot; re-checked
    // against the remote version at fire time.
    reloadTimer_ = new QTimer(this);
    reloadTimer_->setSingleShot(true);
    connect(reloadTimer_, &QTimer::timeout, this, [this] {
      if (session_->address().isEmpty() || session_->id().isEmpty()) return;
      if (!h_.syncToServer()) return;
      // openServerProject holds remoteReloading_ for its whole async lifetime; keep the pending
      // flag and re-poll until it clears.
      if (*remoteReloading_) {
        if (reloadPending_) reloadTimer_->start(50);
        return;
      }
      // A local edit is in flight: our push wins last-writer-wins, then we reload the merged
      // result.
      if (*remotePushing_ || (pushTimer_ && pushTimer_->isActive())) {
        reloadPending_ = true;
        reloadTimer_->start(150);
        return;
      }
      if (!reloadPending_) return;   // nothing queued → nothing to do
      reloadPending_ = false;
      // An event landing mid-reload re-arms this timer, so we converge afterward.
      h_.openServerProject(session_->address(), session_->id(), /*silent=*/true);
    });
  }

  void RemoteSyncController::scheduleRemotePush() {
    if (h_.incognito() || *remoteReloading_ || session_->address().isEmpty() || !h_.syncToServer()) return;
    // Trailing debounce capped by a max-wait, so continuous editing still flushes every ~1.5s.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (pushBurstStart_ == 0) pushBurstStart_ = now;
    const int wait = std::clamp<int>(1500 - static_cast<int>(now - pushBurstStart_), 0, 350);
    pushTimer_->start(wait);
  }

  void RemoteSyncController::startRemotePoll() {
    if (pollTimer_ && !session_->address().isEmpty()) pollTimer_->start();
    // The poll is a backstop for https servers / a dropped socket.
    ensureLiveFeed();
  }

  void RemoteSyncController::stopRemotePoll() {
    if (pollTimer_) pollTimer_->stop();
    if (reloadTimer_) reloadTimer_->stop();
    if (liveFeed_) liveFeed_->unsubscribe();
  }

  // No-op unless server-linked and connected; subscribe() is idempotent for the same origin.
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

  // Mirrors the browser's onServerProjectEvent + shouldReloadFromEvent guards; the reload runs
  // from the timer so a burst coalesces.
  void RemoteSyncController::onRemoteProjectEvent(const QString& id, qint64 version, bool deleted) {
    if (session_->address().isEmpty() || session_->id().isEmpty()) return;
    if (id != session_->id()) return;
    // The linked project was deleted: detach fully, sync toggle or not, or the golden frame
    // outlives it.
    if (deleted) {
      stopRemotePoll();
      if (h_.serverProjectDeleted) h_.serverProjectDeleted();
      return;
    }
    if (!h_.syncToServer()) return;
    if (version <= session_->version()) return;  // our own save echo, or stale
    // reloadPending_ is set here, not only in the timer, so an event arriving mid-reload is
    // remembered.
    reloadPending_ = true;
    if (reloadTimer_) reloadTimer_->start(40);
  }

  // Skipped while a local edit is pending, so we never clobber the user's work or reload our own
  // change.
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
      // Re-check at completion: the session may have changed or a push started while the GET was
      // in flight.
      if (session_->address() != addr || session_->id() != id) return;
      if (*remotePushing_ || (pushTimer_ && pushTimer_->isActive())) return;
      if (meta.version > session_->version())
        h_.openServerProject(addr, id, /*silent=*/true);
    });
  }

}  // namespace stencil::gui

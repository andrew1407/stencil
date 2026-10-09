#include "RemoteSyncController.hpp"
#include "LiveFeed.hpp"
#include "RemoteSession.hpp"
#include "ServerClient.hpp"
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace stencil::gui {

  RemoteSyncController::RemoteSyncController(QObject* parent, RemoteSession* session,
                                            const bool* remoteReloading,
                                            const bool* remotePushing, const bool* planRunning,
                                            Hooks hooks)
      : QObject(parent), session(session), remoteReloading(remoteReloading),
        remotePushing(remotePushing), planRunning(planRunning), h(std::move(hooks)) {
    pushTimer = new QTimer(this);
    pushTimer->setSingleShot(true);
    connect(pushTimer, &QTimer::timeout, this, [this] { firePush(); });
    pollTimer = new QTimer(this);
    pollTimer->setInterval(tableMs("POLL", "remoteMs", POLL_MS));   // backstop behind the live push feed
    connect(pollTimer, &QTimer::timeout, this, [this] { pollRemoteForUpdate(); });
    // Coalesce a burst of live-feed events into one reload, off the socket read slot; re-checked
    // against the remote version at fire time.
    reloadTimer = new QTimer(this);
    reloadTimer->setSingleShot(true);
    connect(reloadTimer, &QTimer::timeout, this, [this] {
      if (this->session->address().isEmpty() || this->session->id().isEmpty()) return;
      if (!h.syncToServer()) return;
      // openServerProject holds remoteReloading for its whole async lifetime, an op plan holds
      // planRunning for its own; a swap under either would change the canvas mid-edit.
      if (*this->remoteReloading || *this->planRunning) {
        if (reloadPending) reloadTimer->start(50);
        return;
      }
      // A local edit is in flight: our push wins last-writer-wins, then we reload the merged
      // result.
      if (localWriteBusy() || resultInFlight) {
        reloadPending = true;
        reloadTimer->start(150);
        return;
      }
      if (!reloadPending) return;   // nothing queued → nothing to do
      reloadPending = false;
      if (reloadVersion <= this->session->version()) return;   // already caught up
      // An event landing mid-reload re-arms this timer, so we converge afterward.
      h.openServerProject(this->session->address(), this->session->id(), /*silent=*/true);
    });
    resultTimer = new QTimer(this);
    resultTimer->setSingleShot(true);
    connect(resultTimer, &QTimer::timeout, this, [this] {
      if (!resultDirty || resultInFlight) return;
      if (this->session->address().isEmpty()) { resultDirty = false; return; }
      if (localWriteBusy() || *this->remoteReloading || *this->planRunning) {
        resultTimer->start(200);
        return;
      }
      startResultUpload();
    });
  }

  int RemoteSyncController::tableMs(const char* section, const char* key, int fallback) {
    static const QJsonObject table = [] {
      QFile f(QStringLiteral(":/config/constants.json"));
      return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
    }();
    const int v = table.value(QLatin1String(section)).toObject().value(QLatin1String(key)).toInt();
    return v > 0 ? v : fallback;
  }

  bool RemoteSyncController::localWriteBusy() const {
    return *remotePushing || (pushTimer && pushTimer->isActive());
  }

  void RemoteSyncController::scheduleRemotePush() {
    if (h.incognito() || *remoteReloading || session->address().isEmpty() || !h.syncToServer()) return;
    // Trailing debounce capped by a max-wait, so continuous editing still flushes every ~1.5s.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (pushBurstStart == 0) pushBurstStart = now;
    const int wait = std::clamp<int>(1500 - static_cast<int>(now - pushBurstStart), 0, 350);
    pushTimer->start(wait);
  }

  void RemoteSyncController::startRemotePoll() {
    if (pollTimer && !session->address().isEmpty()) pollTimer->start();
    // The poll is a backstop for https servers / a dropped socket.
    ensureLiveFeed();
  }

  void RemoteSyncController::stopRemotePoll() {
    if (pollTimer) pollTimer->stop();
    if (reloadTimer) reloadTimer->stop();
    if (liveFeed) liveFeed->unsubscribe();
    // The canvas is leaving the project: a result not yet rendered would bake the wrong one, and
    // one in flight answers for a project no longer held.
    resultDirty = false;
    if (resultTimer) resultTimer->stop();
    if (resultInFlight) {
      ++resultSeq;
      resultInFlight = false;
      if (resultSettled) std::exchange(resultSettled, {})();
    }
  }

  // No-op unless server-linked and connected; subscribe() is idempotent for the same origin.
  void RemoteSyncController::ensureLiveFeed() {
    const QString addr = session->address();
    if (addr.isEmpty()) return;
    stencil::net::ConnectionManager* mgr = session->getConnections();
    stencil::net::ServerClient* c = mgr ? mgr->find(addr) : nullptr;
    if (!c) return;
    if (!liveFeed) {
      liveFeed = new stencil::net::LiveFeed(this);
      connect(liveFeed, &stencil::net::LiveFeed::projectUpdated,
              this, &RemoteSyncController::onRemoteProjectEvent);
    }
    liveFeed->subscribe(addr, c->getToken());
  }

  // Mirrors the browser's onServerProjectEvent + shouldReloadFromEvent guards; the reload runs
  // from the timer so a burst coalesces.
  void RemoteSyncController::onRemoteProjectEvent(const QString& id, qint64 version, bool deleted) {
    if (session->address().isEmpty() || session->id().isEmpty()) return;
    if (id != session->id()) return;
    // The linked project was deleted: detach fully, sync toggle or not, or the golden frame
    // outlives it.
    if (deleted) {
      stopRemotePoll();
      if (h.serverProjectDeleted) h.serverProjectDeleted();
      return;
    }
    if (!h.syncToServer()) return;
    if (version <= session->version()) return;  // our own save echo, or stale
    // reloadPending is set here, not only in the timer, so an event arriving mid-reload is
    // remembered.
    reloadPending = true;
    reloadVersion = std::max(reloadVersion, version);
    if (reloadTimer) reloadTimer->start(40);
  }

  // Skipped while a local edit or an op plan is in flight, so we never clobber the user's work or
  // reload our own change.
  void RemoteSyncController::pollRemoteForUpdate() {
    const QString addr = session->address();
    const QString id = session->id();
    if (addr.isEmpty() || id.isEmpty()) return;
    if (!h.syncToServer()) return;  // sync off — don't pull peer changes over local edits
    if (localWriteBusy() || resultInFlight || *planRunning || *remoteReloading) return;
    stencil::net::ConnectionManager* mgr = session->getConnections();
    stencil::net::ServerClient* c = mgr ? mgr->find(addr) : nullptr;
    if (!c) return;
    QPointer<RemoteSyncController> self(this);
    c->getProjectAsync(id, [this, self, addr, id](bool ok, stencil::net::ServerProject meta,
                                                  QJsonObject) {
      if (!self || !ok) return;
      // Re-check at completion: the session may have changed or a push started while the GET was
      // in flight.
      if (session->address() != addr || session->id() != id) return;
      if (localWriteBusy() || resultInFlight || *planRunning || *remoteReloading) return;
      if (meta.version > session->version())
        h.openServerProject(addr, id, /*silent=*/true);
    });
  }

}  // namespace stencil::gui

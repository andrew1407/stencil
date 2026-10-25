#include "RemoteSession.hpp"
#include "Notifications.hpp"
#include "ServerClient.hpp"
#include "connectionStore.hpp"
#include <QJsonObject>
#include <QPointer>

namespace stencil::gui {

  stencil::net::ConnectionManager* RemoteSession::ensureConnections(QObject* owner) {
    if (!connections) {
      connections = new stencil::net::ConnectionManager(owner);
      connect(connections, &stencil::net::ConnectionManager::changed, owner, [this] {
        stencil::net::connectionStore::saveKeeping(connections->snapshot(), connections->getForgotten());
        // A forgotten server takes its token with it.
        if (!link.address.isEmpty() && !connections->find(link.address)) emit linkedServerForgotten();
      });
    }
    return connections;
  }

  void RemoteSession::warnInsecureConnections() {
    if (!connections) return;
    QStringList insecure;
    for (auto* c : connections->getClients())
      if (stencil::net::ServerClient::isInsecureRemote(c->getBase())) insecure << c->getBase();
    if (insecure.isEmpty()) return;
    notify->error(
        QString("Insecure connection: %1 uses plaintext http — your access token and "
                "images are sent unencrypted. Use https on untrusted networks.")
            .arg(insecure.join(", ")));
  }

  stencil::net::ServerClient* RemoteSession::requireClient(const QString& url, const QString& msg) {
    stencil::net::ServerClient* c = connections ? connections->find(url) : nullptr;
    if (!c) {
      notify->error(msg);
      return nullptr;
    }
    return c;
  }

  bool RemoteSession::saveOutcomeChanged(bool ok) {
    const QString key = link.address + QLatin1Char('\n') + link.id;
    if (key != outcomeKey) { outcomeKey = key; lastOutcome = -1; }
    const bool changed = lastOutcome != (ok ? 1 : 0);
    lastOutcome = ok ? 1 : 0;
    return changed;
  }

  void RemoteSession::noteAdoptedOriginal(const QString& originalHash, qint64 imageKey) {
    adoptedKey = link.address + QLatin1Char('\n') + link.id;
    adoptedHash = originalHash;
    adoptedImageKey = imageKey;
  }

  bool RemoteSession::isAdoptedOriginal(const QString& originalHash, qint64 imageKey) const {
    return !originalHash.isEmpty() && originalHash == adoptedHash && imageKey == adoptedImageKey
        && adoptedKey == link.address + QLatin1Char('\n') + link.id;
  }

  void RemoteSession::putVersionGuardedAsync(
      stencil::net::ServerClient* c, const QString& id,
      std::function<void(qint64 version,
                         std::function<void(bool ok, qint64 newVersion, bool conflict)> cb)> put,
      std::function<void(bool ok, qint64 outVersion)> done) {
    typedef stencil::net::ServerClient::GuardOutcome GO;
    auto outVersion = std::make_shared<qint64>(0);
    stencil::net::ServerClient::runGuardedWriteAsync(
        /*attempts=*/4, /*startVersion=*/0,
        [c, id, put, outVersion](qint64 /*version*/, std::function<void(GO)> cb) {
          // Each attempt re-reads the current version (identity policy).
          c->getProjectAsync(id, [put, outVersion, cb](bool ok, stencil::net::ServerProject meta,
                                                       QJsonObject) {
            if (!ok) { cb(GO::FAILED); return; }  // read failed; lastError() set
            put(meta.version, [outVersion, cb](bool pok, qint64 nv, bool conflict) {
              if (pok) { *outVersion = nv; cb(GO::COMMITTED); return; }
              cb(conflict ? GO::CONFLICT : GO::FAILED);
            });
          });
        },
        // `resolve` is a no-op, so the start version is unused.
        [](qint64, std::function<void(bool, qint64)> cb) { cb(true, 0); },
        [done, outVersion](GO r) { done(r == GO::COMMITTED, *outVersion); });
  }

  void RemoteSession::putMetaAsync(
      stencil::net::ServerClient* c, const QString& address, const QString& id,
      std::function<void(qint64 version, std::function<void(bool ok, qint64 newVersion, bool conflict)> cb)> put,
      std::function<void(RemoteLink&)> ownLink, std::function<void(bool ok, qint64 outVersion)> done) {
    const qint64 before = link.version;
    QPointer<RemoteSession> self(this);
    putVersionGuardedAsync(c, id, std::move(put), [self, address, id, before, ownLink, done](bool ok, qint64 nv) {
      if (!self) return;
      if (ok && self->link.id == id && self->link.address == address) {
        if (ownLink) ownLink(self->link);
        adoptOwnFileVersion(self->link, before, nv);
      }
      if (done) done(ok, nv);
    });
  }

}  // namespace stencil::gui

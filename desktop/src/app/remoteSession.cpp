#include "remoteSession.hpp"
#include "notifications.hpp"
#include "serverClient.hpp"
#include <QJsonObject>

namespace stencil::gui {

  RemoteSession::RemoteSession(QObject* parent, Notifications* notify)
      : QObject(parent), notify_(notify) {}

  stencil::net::ServerClient* RemoteSession::requireClient(const QString& url, const QString& msg) {
    stencil::net::ServerClient* c = connections_ ? connections_->find(url) : nullptr;
    if (!c) {
      notify_->error(msg);
      return nullptr;
    }
    return c;
  }

  void RemoteSession::putVersionGuardedAsync(
      stencil::net::ServerClient* c, const QString& id,
      std::function<void(qint64 version,
                         std::function<void(bool ok, qint64 newVersion, bool conflict)> cb)> put,
      std::function<void(bool ok, qint64 outVersion)> done) {
    using GO = stencil::net::ServerClient::GuardOutcome;
    // Winning version threaded through the heap-managed loop to the final `done`.
    auto outVersion = std::make_shared<qint64>(0);
    stencil::net::ServerClient::runGuardedWriteAsync(
        /*attempts=*/4, /*startVersion=*/0,
        [c, id, put, outVersion](qint64 /*version*/, std::function<void(GO)> cb) {
          // Each attempt re-reads the current version itself (identity policy).
          c->getProjectAsync(id, [put, outVersion, cb](bool ok, stencil::net::ServerProject meta,
                                                       QJsonObject) {
            if (!ok) { cb(GO::Failed); return; }  // read failed; lastError() set
            put(meta.version, [outVersion, cb](bool pok, qint64 nv, bool conflict) {
              if (pok) { *outVersion = nv; cb(GO::Committed); return; }
              cb(conflict ? GO::Conflict : GO::Failed);
            });
          });
        },
        // `resolve` is a no-op (each attempt re-reads), so the start version is unused.
        [](qint64, std::function<void(bool, qint64)> cb) { cb(true, 0); },
        [done, outVersion](GO r) { done(r == GO::Committed, *outVersion); });
  }

}  // namespace stencil::gui

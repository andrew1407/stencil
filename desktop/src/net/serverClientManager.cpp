#include "serverClient.hpp"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace stencil::net {

  ConnectionManager::ConnectionManager(QObject* parent) : QObject(parent) {}

  ConnectionManager::~ConnectionManager() {
    qDeleteAll(clients_);
    qDeleteAll(pending_);   // severs any handshake still in flight
  }

  void ConnectionManager::connectToAsync(const QString& url, const QString& token,
                                        std::function<void(bool, QString)> done,
                                        ServerClient::CredentialKind kindHint) {
    // Invite link: a "#token=<tok>" fragment supplies the credential — split it off
    // before normalization (which drops fragments). An explicitly-typed token wins.
    QString linkToken;
    const QString stripped = ServerClient::splitInviteToken(url, linkToken);
    const QString cred = token.isEmpty() ? linkToken : token;
    const QString base = ServerClient::normalizeBase(stripped);
    if (find(base)) {
      done(false, QStringLiteral("already connected"));
      return;
    }
    auto* client = new ServerClient(base);
    pending_.push_back(client);
    // The hint is only ever supplied by a caller REUSING a proven credential (the saved
    // set); a freshly typed or invite-link token arrives without one and probes first.
    client->connectAsync(cred, [this, client, done](bool ok) {
      pending_.removeOne(client);
      // A REFUSED CREDENTIAL keeps its place: the server is fine and the URL worth
      // keeping, so the row can offer a sign-in. An unreachable host is still
      // dropped — there is nothing to sign in to.
      if (!ok && !client->needsReauth()) {
        const QString err = client->lastError();
        delete client;
        done(false, err);
        return;
      }
      clients_.push_back(client);
      emit changed();
      done(ok, ok ? QString() : client->lastError());
    }, kindHint);
  }

  void ConnectionManager::disconnectFrom(const QString& url) {
    if (clients_.isEmpty()) return;
    if (url.isEmpty()) {
      delete clients_.takeLast();
      emit changed();
      return;
    }
    const QString base = ServerClient::normalizeBase(url);
    for (int i = 0; i < clients_.size(); ++i) {
      if (clients_[i]->base() == base) {
        delete clients_.takeAt(i);
        emit changed();
        return;
      }
    }
  }

  void ConnectionManager::reorder(int from, int to) {
    if (from < 0 || from >= clients_.size()) return;
    if (to < 0) to = 0;
    if (to >= clients_.size()) to = clients_.size() - 1;
    if (from == to) return;
    clients_.move(from, to);
    emit changed();
  }

  void ConnectionManager::reauthenticateAsync(const QString& url, const QString& token,
                                             std::function<void(bool, QString)> done) {
    ServerClient* c = find(url);
    if (!c) {
      connectToAsync(url, token, std::move(done));   // nothing listed: an ordinary connect
      return;
    }
    // The client is REUSED, credential and all (connectAsync re-proves the kind), so the row
    // keeps its place and its identity. connectToAsync() answered "already connected" and
    // left the session expired however good the pasted token was.
    c->connectAsync(token, [this, c, done](bool ok) {
      emit changed();
      done(ok, ok ? QString() : c->lastError());
    });
  }

  void ConnectionManager::reconnectAllAsync(std::function<void()> done) {
    if (clients_.isEmpty()) {
      emit changed();
      if (done) done();
      return;
    }
    auto remaining = std::make_shared<int>(clients_.size());
    for (auto* c : clients_) {
      c->reconnectAsync([this, remaining, done](bool) {
        if (--*remaining == 0) {
          emit changed();
          if (done) done();
        }
      });
    }
  }

  QStringList ConnectionManager::urls() const {
    QStringList out;
    for (auto* c : clients_) out << c->base();
    return out;
  }

  ServerClient* ConnectionManager::find(const QString& url) const {
    const QString base = ServerClient::normalizeBase(url);
    for (auto* c : clients_)
      if (c->base() == base) return c;
    return nullptr;
  }

  QVector<SavedServer> ConnectionManager::snapshot() const {
    QVector<SavedServer> out;
    out.reserve(clients_.size());
    // Persist the CREDENTIAL, never the minted session token — sessions die
    // with a server restart; the credential re-mints on the next connect. Its KIND
    // rides along so the next launch knows an admin credential without re-probing.
    for (auto* c : clients_)
      out.push_back({c->base(), c->credential(), ServerClient::kindTag(c->credentialKind())});
    return out;
  }

  void ConnectionManager::sharedProjectsAsync(
      std::function<void(QVector<ServerProject>)> done) const {
    if (clients_.isEmpty()) {
      done({});
      return;
    }
    // Fan out an async list to every client; merge the image-bearing projects and fire `done` once
    // the last list resolves. Heap-managed counter + accumulator survive across the async hops.
    auto remaining = std::make_shared<int>(clients_.size());
    auto out = std::make_shared<QVector<ServerProject>>();
    for (auto* c : clients_) {
      c->listProjectsAsync([remaining, out, done](bool ok, QVector<ServerProject> ps) {
        if (ok)
          for (const auto& p : ps)
            if (p.hasImage) out->push_back(p);
        if (--*remaining == 0) done(*out);
      });
    }
  }
}  // namespace stencil::net


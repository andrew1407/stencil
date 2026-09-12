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
    // The "#token=<tok>" fragment is split off before normalization drops it; a typed token wins.
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
    client->connectAsync(cred, [this, client, done](bool ok) {
      pending_.removeOne(client);
      // A REFUSED CREDENTIAL keeps its place so the row can offer a sign-in; an unreachable host is dropped.
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
    // The client is REUSED so the row keeps its place; connectToAsync() would answer "already connected".
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
    // Persist the CREDENTIAL (a minted session dies with a server restart) and its KIND.
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


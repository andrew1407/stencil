#include "ServerClient.hpp"

#include <QHostAddress>
#include <QHostInfo>
#include <QUrl>


namespace stencil::net {

  ConnectionManager::ConnectionManager(QObject* parent) : QObject(parent) {}

  ConnectionManager::~ConnectionManager() {
    qDeleteAll(clients);
    qDeleteAll(pending);   // severs any handshake still in flight
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
    if (ServerClient::isRefusedTarget(base)) {
      done(false, QStringLiteral("refusing a link-local, metadata or reserved server address"));
      return;
    }
    // A name is judged by what it resolves to, as the CLI and the bot judge it; a failed lookup is
    // left for the handshake to report.
    const QString host = QUrl(base).host();
    if (QHostAddress().setAddress(host)) {
      dial(base, cred, std::move(done), kindHint);
      return;
    }
    QHostInfo::lookupHost(host, this, [this, base, cred, done = std::move(done), kindHint](const QHostInfo& info) mutable {
      if (ServerClient::refusesResolved(info.addresses())) {
        done(false, QStringLiteral("refusing a server whose name resolves to a link-local, metadata or reserved address"));
        return;
      }
      if (find(base)) {
        done(false, QStringLiteral("already connected"));
        return;
      }
      dial(base, cred, std::move(done), kindHint);
    });
  }

  void ConnectionManager::dial(const QString& base, const QString& cred,
                               std::function<void(bool, QString)> done,
                               ServerClient::CredentialKind kindHint) {
    auto* client = new ServerClient(base);
    pending.push_back(client);
    client->connectAsync(cred, [this, client, done](bool ok) {
      pending.removeOne(client);
      // A REFUSED CREDENTIAL keeps its place so the row can offer a sign-in; an unreachable host is dropped.
      if (!ok && !client->needsReauth()) {
        const QString err = client->lastError();
        delete client;
        done(false, err);
        return;
      }
      clients.push_back(client);
      forgotten.removeAll(client->getBase());
      emit changed();
      done(ok, ok ? QString() : client->lastError());
    }, kindHint);
  }

  void ConnectionManager::connectSessionOnlyAsync(const QString& url, const QString& token,
                                                  const SavedServer& saved,
                                                  std::function<void(bool, QString)> done) {
    const QString base = ServerClient::normalizeBase(url);
    if (!find(base)) sessionOnly.insert(base, {base, saved.token, saved.kind});
    connectToAsync(url, token, [this, base, done](bool ok, QString err) {
      if (!find(base)) sessionOnly.remove(base);
      done(ok, err);
    });
  }

  void ConnectionManager::disconnectFrom(const QString& url) {
    if (clients.isEmpty()) return;
    if (url.isEmpty()) {
      sessionOnly.remove(clients.last()->getBase());
      forgotten << clients.last()->getBase();
      delete clients.takeLast();
      emit changed();
      return;
    }
    const QString base = ServerClient::normalizeBase(url);
    for (int i = 0; i < clients.size(); ++i) {
      if (clients[i]->getBase() == base) {
        sessionOnly.remove(base);
        forgotten << base;
        delete clients.takeAt(i);
        emit changed();
        return;
      }
    }
  }

  void ConnectionManager::reorder(int from, int to) {
    if (from < 0 || from >= clients.size()) return;
    if (to < 0) to = 0;
    if (to >= clients.size()) to = clients.size() - 1;
    if (from == to) return;
    clients.move(from, to);
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
    // A token the user signs in with is theirs to keep.
    sessionOnly.remove(c->getBase());
    c->connectAsync(token, [this, c, done](bool ok) {
      emit changed();
      done(ok, ok ? QString() : c->lastError());
    });
  }

  void ConnectionManager::reconnectAllAsync() {
    if (clients.isEmpty()) {
      emit changed();
      return;
    }
    auto remaining = std::make_shared<int>(clients.size());
    for (auto* c : clients) {
      c->reconnectAsync([this, remaining](bool) {
        if (--*remaining == 0) emit changed();
      });
    }
  }

  QStringList ConnectionManager::urls() const {
    QStringList out;
    for (auto* c : clients) out << c->getBase();
    return out;
  }

  ServerClient* ConnectionManager::find(const QString& url) const {
    const QString base = ServerClient::normalizeBase(url);
    for (auto* c : clients)
      if (c->getBase() == base) return c;
    return nullptr;
  }

  QVector<SavedServer> ConnectionManager::snapshot() const {
    QVector<SavedServer> out;
    out.reserve(clients.size());
    // Persist the CREDENTIAL (a minted session dies with a server restart) and its KIND.
    for (auto* c : clients) {
      const auto kept = sessionOnly.constFind(c->getBase());
      out.push_back(kept != sessionOnly.constEnd()
                        ? *kept
                        : SavedServer{c->getBase(), c->getCredential(), ServerClient::kindTag(c->credentialKind())});
    }
    return out;
  }

  void ConnectionManager::sharedProjectsAsync(
      std::function<void(QVector<ServerProject>)> done) const {
    if (clients.isEmpty()) {
      done({});
      return;
    }
    auto remaining = std::make_shared<int>(clients.size());
    auto out = std::make_shared<QVector<ServerProject>>();
    for (auto* c : clients) {
      c->listProjectsAsync([remaining, out, done](bool ok, QVector<ServerProject> ps) {
        if (ok)
          for (const auto& p : ps)
            if (p.hasImage) out->push_back(p);
        if (--*remaining == 0) done(*out);
      });
    }
  }
}  // namespace stencil::net


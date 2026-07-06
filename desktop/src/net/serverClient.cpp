#include "serverClient.hpp"

#include <QEventLoop>
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

  ServerClient::ServerClient(const QString& url)
      : nam_(new QNetworkAccessManager), base_(normalizeBase(url)) {}

  ServerClient::~ServerClient() { delete nam_; }

  bool ServerClient::isLoopbackHost(const QString& host) {
    if (host.isEmpty()) return false;
    if (host.compare("localhost", Qt::CaseInsensitive) == 0) return true;
    if (host.endsWith(".localhost", Qt::CaseInsensitive)) return true;
    const QHostAddress addr(host);
    return !addr.isNull() && addr.isLoopback();  // 127.0.0.0/8, ::1
  }

  bool ServerClient::isInsecureRemote(const QString& base) {
    const QUrl u(base);
    return u.scheme().compare("http", Qt::CaseInsensitive) == 0 && !isLoopbackHost(u.host());
  }

  QString ServerClient::normalizeBase(const QString& raw) {
    QString s = raw.trimmed();
    if (s.isEmpty()) return s;
    if (!s.startsWith("http://", Qt::CaseInsensitive) &&
        !s.startsWith("https://", Qt::CaseInsensitive)) {
      // Secure by default: a bare host gets https, EXCEPT loopback (localhost dev servers
      // speak plaintext http and never leave the machine). An explicit "http://<remote>"
      // still works — the user opts into cleartext and the UI warns about it.
      const QString host = QUrl("http://" + s).host();
      s = (isLoopbackHost(host) ? QStringLiteral("http://") : QStringLiteral("https://")) + s;
    }
    QUrl u(s);
    // Keep scheme + authority only (drop any path / trailing slash).
    QString origin = u.scheme() + "://" + u.authority();
    return origin;
  }

  QNetworkRequest ServerClient::buildRequest(const QString& path,
                                             const QString& contentType) const {
    QNetworkRequest req{QUrl(base_ + path)};
    // Bound every request so a hung/malicious server can't wedge a transfer forever; the
    // reply then finishes with a timeout error.
    req.setTransferTimeout(20000);
    if (!token_.isEmpty())
      req.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    if (!contentType.isEmpty())
      req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    return req;
  }

  QByteArray ServerClient::request(const QByteArray& method, const QString& path,
                                   const QByteArray& body, const QString& contentType,
                                   int& status) {
    status = 0;
    QNetworkRequest req = buildRequest(path, contentType);
    // Synchronous: blocks on a nested event loop until the reply finishes. Retained only
    // for the not-yet-converted call sites; prefer requestAsync (no re-entrancy).
    QNetworkReply* reply = nam_->sendCustomRequest(req, method, body);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    if (reply->error() != QNetworkReply::NoError && status == 0)
      err_ = reply->errorString();
    reply->deleteLater();
    return data;
  }

  void ServerClient::requestAsync(const QByteArray& method, const QString& path,
                                  const QByteArray& body, const QString& contentType,
                                  std::function<void(int status, QByteArray body)> done) {
    QNetworkRequest req = buildRequest(path, contentType);
    QNetworkReply* reply = nam_->sendCustomRequest(req, method, body);
    // Context object is nam_ (a QObject owned by this client): if the client is destroyed
    // nam_ dies with it, the connection is severed and this slot never runs on a dangling
    // `this`. deleteLater keeps the reply alive until the slot returns.
    QObject::connect(reply, &QNetworkReply::finished, nam_,
                     [this, reply, done = std::move(done)]() {
                       const int status =
                           reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                       const QByteArray data = reply->readAll();
                       if (reply->error() != QNetworkReply::NoError && status == 0)
                         err_ = reply->errorString();
                       reply->deleteLater();
                       done(status, data);
                     });
  }

  bool ServerClient::connect(const QString& token) {
    status_ = Status::Connecting;
    const auto fail = [this](const QString& msg) {
      err_ = msg;
      status_ = Status::Error;
      return false;
    };
    if (base_.isEmpty()) return fail("empty server URL");
    int status = 0;
    if (token.isEmpty()) {
      const QByteArray body =
          request("POST", "/auth/token", "{}", "application/json", status);
      if (status < 200 || status >= 300)
        return fail(QString("token request failed (HTTP %1)").arg(status));
      const QJsonObject obj = QJsonDocument::fromJson(body).object();
      token_ = obj.value("token").toString();
      if (token_.isEmpty()) return fail("server returned no token");
    } else {
      token_ = token;
      request("GET", "/projects", {}, {}, status);
      if (status < 200 || status >= 300) {
        token_.clear();
        return fail(QString("token rejected (HTTP %1)").arg(status));
      }
    }
    status_ = Status::Connected;
    return true;
  }

  // ── Async REST surface (mirrors the synchronous methods above op-for-op) ──

  void ServerClient::connectAsync(const QString& token, std::function<void(bool)> done) {
    status_ = Status::Connecting;
    if (base_.isEmpty()) {
      err_ = "empty server URL";
      status_ = Status::Error;
      done(false);
      return;
    }
    if (token.isEmpty()) {
      requestAsync("POST", "/auth/token", "{}", "application/json",
                   [this, done = std::move(done)](int status, QByteArray body) {
                     if (status < 200 || status >= 300) {
                       err_ = QString("token request failed (HTTP %1)").arg(status);
                       status_ = Status::Error;
                       done(false);
                       return;
                     }
                     token_ = QJsonDocument::fromJson(body).object().value("token").toString();
                     if (token_.isEmpty()) {
                       err_ = "server returned no token";
                       status_ = Status::Error;
                       done(false);
                       return;
                     }
                     status_ = Status::Connected;
                     done(true);
                   });
    } else {
      token_ = token;
      requestAsync("GET", "/projects", {}, {},
                   [this, done = std::move(done)](int status, QByteArray) {
                     if (status < 200 || status >= 300) {
                       token_.clear();
                       err_ = QString("token rejected (HTTP %1)").arg(status);
                       status_ = Status::Error;
                       done(false);
                       return;
                     }
                     status_ = Status::Connected;
                     done(true);
                   });
    }
  }

  void ServerClient::reconnectAsync(std::function<void(bool)> done) {
    // Re-validate the token we hold; if it's been rejected/cleared, issue a fresh one.
    if (token_.isEmpty()) {
      connectAsync(QString(), std::move(done));
      return;
    }
    connectAsync(token_, [this, done](bool ok) {
      if (ok) {
        done(true);
        return;
      }
      token_.clear();
      connectAsync(QString(), done);
    });
  }

  void ServerClient::listProjectsAsync(
      std::function<void(bool, QVector<ServerProject>)> done) {
    requestAsync("GET", "/projects", {}, {},
                 [this, done = std::move(done)](int status, QByteArray body) {
                   QVector<ServerProject> out;
                   if (status < 200 || status >= 300) {
                     err_ = QString("list failed (HTTP %1)").arg(status);
                     done(false, out);
                     return;
                   }
                   const QJsonArray arr =
                       QJsonDocument::fromJson(body).object().value("projects").toArray();
                   for (const QJsonValue& v : arr) {
                     const QJsonObject o = v.toObject();
                     ServerProject p;
                     p.id = o.value("id").toString();
                     p.name = o.value("name").toString();
                     p.color = o.value("color").toString();
                     p.hasImage = o.value("hasImage").toBool();
                     p.imageW = o.value("imageW").toInt();
                     p.imageH = o.value("imageH").toInt();
                     p.source = o.value("source").toString();
                     p.resource = o.value("resource").toString();
                     p.createdAt = static_cast<qint64>(o.value("createdAt").toDouble());
                     p.updatedAt = static_cast<qint64>(o.value("updatedAt").toDouble());
                     p.expiresAt = static_cast<qint64>(o.value("expiresAt").toDouble());
                     p.version = static_cast<qint64>(o.value("version").toDouble());
                     p.serverUrl = base_;
                     out.push_back(p);
                   }
                   done(true, out);
                 });
  }

  void ServerClient::createProjectAsync(
      const QString& name, const QString& source, const QString& resource, bool hasImage,
      int w, int h, std::function<void(bool, QString, qint64)> done) {
    QJsonObject obj;
    obj.insert("name", name);
    obj.insert("source", source);
    obj.insert("resource", resource);
    obj.insert("hasImage", hasImage);
    obj.insert("imageW", w);
    obj.insert("imageH", h);
    requestAsync("POST", "/projects", QJsonDocument(obj).toJson(QJsonDocument::Compact),
                 "application/json",
                 [this, done = std::move(done)](int status, QByteArray body) {
                   if (status < 200 || status >= 300) {
                     err_ = QString("create failed (HTTP %1)").arg(status);
                     done(false, QString(), 0);
                     return;
                   }
                   const QJsonObject rec = QJsonDocument::fromJson(body).object();
                   const QString id = rec.value("id").toString();
                   const qint64 ver = static_cast<qint64>(rec.value("version").toDouble());
                   done(!id.isEmpty(), id, ver);
                 });
  }

  void ServerClient::getProjectAsync(
      const QString& id, std::function<void(bool, ServerProject, QJsonObject)> done) {
    requestAsync("GET", QString("/projects/%1").arg(id), {}, {},
                 [this, done = std::move(done)](int status, QByteArray body) {
                   ServerProject meta;
                   QJsonObject layoutOut;
                   if (status < 200 || status >= 300) {
                     err_ = QString("get failed (HTTP %1)").arg(status);
                     done(false, meta, layoutOut);
                     return;
                   }
                   const QJsonObject root = QJsonDocument::fromJson(body).object();
                   const QJsonObject p = root.value("project").toObject();
                   meta.id = p.value("id").toString();
                   meta.name = p.value("name").toString();
                   meta.color = p.value("color").toString();
                   meta.hasImage = p.value("hasImage").toBool();
                   meta.imageW = p.value("imageW").toInt();
                   meta.imageH = p.value("imageH").toInt();
                   meta.source = p.value("source").toString();
                   meta.resource = p.value("resource").toString();
                   meta.version = static_cast<qint64>(p.value("version").toDouble());
                   meta.serverUrl = base_;
                   layoutOut = root.value("layout").toObject();
                   done(true, meta, layoutOut);
                 });
  }

  namespace {
    QJsonObject withVersion(QJsonObject obj, qint64 version) {
      obj.insert("version", static_cast<double>(version));
      return obj;
    }
  }  // namespace

  void ServerClient::putGuarded(const QString& id, QJsonObject obj, qint64 version,
                                const char* verb,
                                std::function<void(bool, qint64, bool)> done) {
    requestAsync("PUT", QString("/projects/%1").arg(id),
                 QJsonDocument(withVersion(std::move(obj), version)).toJson(QJsonDocument::Compact),
                 "application/json",
                 [this, verb, done = std::move(done)](int status, QByteArray body) {
                   if (status == 409) {
                     err_ = "stale version (edited elsewhere)";
                     done(false, 0, true);
                     return;
                   }
                   if (status < 200 || status >= 300) {
                     err_ = QString("%1 failed (HTTP %2)").arg(QLatin1String(verb)).arg(status);
                     done(false, 0, false);
                     return;
                   }
                   done(true,
                        static_cast<qint64>(
                            QJsonDocument::fromJson(body).object().value("version").toDouble()),
                        false);
                 });
  }

  void ServerClient::updateProjectAsync(
      const QString& id, const QString& name, const QJsonObject& layout, qint64 version,
      std::function<void(bool, qint64, bool)> done) {
    QJsonObject obj;
    if (!name.isEmpty()) obj.insert("name", name);
    obj.insert("layout", layout);
    putGuarded(id, std::move(obj), version, "update", std::move(done));
  }

  void ServerClient::updateProjectColorAsync(
      const QString& id, const QString& color, qint64 version,
      std::function<void(bool, qint64, bool)> done) {
    QJsonObject obj;
    obj.insert("color", color);  // always sent (even "") so a clear reaches the server
    putGuarded(id, std::move(obj), version, "update", std::move(done));
  }

  void ServerClient::updateProjectNameAsync(
      const QString& id, const QString& name, qint64 version,
      std::function<void(bool, qint64, bool)> done) {
    QJsonObject obj;
    obj.insert("name", name);  // colour + layout omitted → server COALESCE leaves them
    putGuarded(id, std::move(obj), version, "rename", std::move(done));
  }

  void ServerClient::uploadFileAsync(const QString& id, const QString& kind,
                                     const QByteArray& bytes, const QString& ext, int w, int h,
                                     std::function<void(bool)> done) {
    QString path = QString("/projects/%1/files/%2").arg(id, kind);
    QUrlQuery q;
    q.addQueryItem("ext", ext);
    q.addQueryItem("w", QString::number(w));
    q.addQueryItem("h", QString::number(h));
    path += "?" + q.toString(QUrl::FullyEncoded);
    requestAsync("POST", path, bytes, "application/octet-stream",
                 [this, done = std::move(done)](int status, QByteArray) {
                   if (status < 200 || status >= 300) {
                     err_ = QString("upload failed (HTTP %1)").arg(status);
                     done(false);
                     return;
                   }
                   done(true);
                 });
  }

  void ServerClient::downloadFileAsync(const QString& id, const QString& kind,
                                       std::function<void(bool, QByteArray)> done) {
    requestAsync("GET", QString("/projects/%1/files/%2").arg(id, kind), {}, {},
                 [this, done = std::move(done)](int status, QByteArray data) {
                   const bool ok = status >= 200 && status < 300;
                   if (!ok) err_ = QString("download failed (HTTP %1)").arg(status);
                   done(ok, data);
                 });
  }

  void ServerClient::deleteProjectAsync(const QString& id, std::function<void(bool)> done) {
    requestAsync("DELETE", QString("/projects/%1").arg(id), {}, {},
                 [this, done = std::move(done)](int status, QByteArray) {
                   if (status < 200 || status >= 300) {
                     err_ = QString("delete failed (HTTP %1)").arg(status);
                     done(false);
                     return;
                   }
                   done(true);
                 });
  }

  void ServerClient::runGuardedWriteAsync(
      int attempts, qint64 startVersion,
      std::function<void(qint64, std::function<void(GuardOutcome)>)> attempt,
      std::function<void(qint64, std::function<void(bool, qint64)>)> resolve,
      std::function<void(GuardOutcome)> done) {
    // Heap-managed loop state so the recursion survives across async hops.
    struct State {
      int i = 0;
      qint64 version = 0;
      int attempts = 0;
      std::function<void(qint64, std::function<void(GuardOutcome)>)> attempt;
      std::function<void(qint64, std::function<void(bool, qint64)>)> resolve;
      std::function<void(GuardOutcome)> done;
      std::function<void()> step;
    };
    auto st = std::make_shared<State>();
    st->version = startVersion;
    st->attempts = attempts;
    st->attempt = std::move(attempt);
    st->resolve = std::move(resolve);
    st->done = std::move(done);
    st->step = [st]() {
      st->attempt(st->version, [st](GuardOutcome o) {
        if (o != GuardOutcome::Conflict) {  // Committed or Failed → done
          st->done(o);
          return;
        }
        if (st->i + 1 >= st->attempts) {  // last attempt still conflicted → exhausted
          st->done(GuardOutcome::Conflict);
          return;
        }
        st->resolve(st->version, [st](bool ok, qint64 newVersion) {
          if (!ok) {  // resolve gave up (e.g. re-read failed)
            st->done(GuardOutcome::Conflict);
            return;
          }
          st->version = newVersion;
          ++st->i;
          st->step();
        });
      });
    };
    st->step();
  }

  ConnectionManager::ConnectionManager(QObject* parent) : QObject(parent) {}

  ConnectionManager::~ConnectionManager() { qDeleteAll(clients_); }

  bool ConnectionManager::connectTo(const QString& url, const QString& token, QString& err) {
    const QString base = ServerClient::normalizeBase(url);
    if (find(base)) {
      err = "already connected";
      return false;
    }
    auto* client = new ServerClient(base);
    if (!client->connect(token)) {
      err = client->lastError();
      delete client;
      return false;
    }
    clients_.push_back(client);
    emit changed();
    return true;
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

  void ConnectionManager::reconnectAsync(const QString& url,
                                         std::function<void(bool, QString)> done) {
    ServerClient* c = find(url);
    if (!c) {
      done(false, QStringLiteral("not connected"));
      return;
    }
    c->reconnectAsync([this, c, done](bool ok) {
      emit changed();  // a now-dead connection still warrants a UI refresh
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
    for (auto* c : clients_) out.push_back({c->base(), c->token()});
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

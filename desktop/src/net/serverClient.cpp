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

  QString ServerClient::splitInviteToken(const QString& raw, QString& token) {
    token.clear();
    const int hash = raw.indexOf('#');
    if (hash < 0) return raw;
    const QString frag = raw.mid(hash + 1);
    if (!frag.startsWith(QLatin1String("token="))) return raw;
    // Decode a browser-encoded token; a plain one passes through unchanged.
    token = QUrl::fromPercentEncoding(frag.mid(6).toUtf8());
    return raw.left(hash);
  }

  QString ServerClient::inviteLink(const QString& base, const QString& token) {
    return base + "#token=" + token;
  }

  QString ServerClient::kindTag(CredentialKind k) {
    return k == CredentialKind::Admin     ? QStringLiteral("admin")
           : k == CredentialKind::Session ? QStringLiteral("session")
                                          : QString();
  }

  ServerClient::CredentialKind ServerClient::kindFromTag(const QString& tag) {
    if (tag == QLatin1String("admin")) return CredentialKind::Admin;
    if (tag == QLatin1String("session")) return CredentialKind::Session;
    return CredentialKind::None;
  }

  // The browser's own failure text (net/connectionManager.js _req): "<METHOD> <path>: <why>",
  // where <why> is the server's JSON `message` when it sent one, else "HTTP <status>". A
  // request that never reached the server has no status at all, and the browser's fetch
  // rejection carries the transport's own message — so that is what is shown, never "HTTP 0".
  static QString restError(const QByteArray& method, const QString& path, int status,
                           const QByteArray& body, const QString& transport) {
    if (status == 0)
      return transport.isEmpty() ? QStringLiteral("the request never reached the server")
                                 : transport;
    QString why = QJsonDocument::fromJson(body).object().value("message").toString();
    if (why.isEmpty()) why = QStringLiteral("HTTP %1").arg(status);
    const int q = path.indexOf('?');   // the browser reports the path, never its query
    return QString("%1 %2: %3").arg(QLatin1String(method), q < 0 ? path : path.left(q), why);
  }

  QNetworkRequest ServerClient::buildRequest(const QString& path,
                                             const QString& contentType,
                                             const QString& bearer) const {
    QNetworkRequest req{QUrl(base_ + path)};
    // Bound every request so a hung/malicious server can't wedge a transfer forever; the
    // reply then finishes with a timeout error.
    req.setTransferTimeout(20000);
    const QString& tok = bearer.isEmpty() ? token_ : bearer;
    if (!tok.isEmpty())
      req.setRawHeader("Authorization", "Bearer " + tok.toUtf8());
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
    if (status < 200 || status >= 300)
      err_ = restError(method, path, status, data,
                       reply->error() == QNetworkReply::NoError ? QString() : reply->errorString());
    reply->deleteLater();
    return data;
  }

  void ServerClient::requestAsync(const QByteArray& method, const QString& path,
                                  const QByteArray& body, const QString& contentType,
                                  std::function<void(int status, QByteArray body)> done,
                                  bool retried) {
    QNetworkRequest req = buildRequest(path, contentType);
    QNetworkReply* reply = nam_->sendCustomRequest(req, method, body);
    // Context object is nam_ (a QObject owned by this client): if the client is destroyed
    // nam_ dies with it, the connection is severed and this slot never runs on a dangling
    // `this`. deleteLater keeps the reply alive until the slot returns.
    QObject::connect(reply, &QNetworkReply::finished, nam_,
                     [this, reply, method, path, body, contentType, retried,
                      done = std::move(done)]() mutable {
                       const int status =
                           reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                       const QByteArray data = reply->readAll();
                       if (status < 200 || status >= 300)
                         err_ = restError(method, path, status, data,
                                          reply->error() == QNetworkReply::NoError
                                              ? QString()
                                              : reply->errorString());
                       reply->deleteLater();
                       const bool refused = status == 401 || status == 403;
                       // A minted session dies with a server restart — while the user's
                       // credential is at hand, re-mint with it once and retry the
                       // request in place (browser/extension parity).
                       if (refused && status_ == Status::Connected && !retried &&
                           !credential_.isEmpty() && path != QLatin1String("/auth/token")) {
                         token_ = credential_;  // the mint carries the credential as bearer
                         requestAsync("POST", "/auth/token", "{}", "application/json",
                                      [this, method, path, body, contentType,
                                       done = std::move(done)](int mint, QByteArray mb) mutable {
                                        const QString tok = QJsonDocument::fromJson(mb)
                                                                .object().value("token").toString();
                                        if (mint < 200 || mint >= 300 || tok.isEmpty()) {
                                          done(mint, {});  // expired was marked by the mint's 401
                                          return;
                                        }
                                        token_ = tok;
                                        requestAsync(
                                            method, path, body, contentType,
                                            [this, done = std::move(done)](int st, QByteArray rb) {
                                              // It minted AND the session works: this
                                              // credential is an admin token (browser parity).
                                              if (st >= 200 && st < 300)
                                                kind_ = CredentialKind::Admin;
                                              done(st, rb);
                                            },
                                            /*retried=*/true);
                                      },
                                      /*retried=*/true);
                         return;
                       }
                       // A live session refused mid-flight (and past rescue) is EXPIRED,
                       // not a dead server: the row offers a reconnect instead of
                       // pretending the host is down. One warning, on the way in.
                       if (refused && status_ == Status::Connected) {
                         status_ = Status::Expired;
                         qWarning("stencil: session on %s expired — reconnect to sign in again",
                                  qPrintable(base_));
                       }
                       done(status, data);
                     });
  }

  bool ServerClient::connect(const QString& token, CredentialKind hint) {
    credential_ = token;
    kind_ = CredentialKind::None;   // re-proven below by whichever path gets in
    status_ = Status::Connecting;
    // A refused credential is Expired, not Error: see the enum's note.
    const auto failAuth = [this](const QString& msg = QString()) {
      if (!msg.isEmpty()) err_ = msg;
      status_ = Status::Expired;
      qWarning("stencil: session on %s needs re-authentication (%s)", qPrintable(base_),
               qPrintable(msg));   // ONE warning, never a repeated error
      return false;
    };
    const auto fail = [this](const QString& msg = QString()) {
      if (!msg.isEmpty()) err_ = msg;
      status_ = Status::Error;
      return false;
    };
    if (base_.isEmpty()) return fail("empty server URL");
    int status = 0;
    if (token.isEmpty()) {
      const QByteArray body =
          request("POST", "/auth/token", "{}", "application/json", status);
      if (status < 200 || status >= 300)
        return status == 401 || status == 403
                   ? failAuth(QString("this server gates token minting (ADMIN_TOKEN) — paste a "
                                      "session token, or the admin token, into the Token field"))
                   : fail();
      const QJsonObject obj = QJsonDocument::fromJson(body).object();
      token_ = obj.value("token").toString();
      if (token_.isEmpty()) return fail("server returned no token");
    } else if (hint == CredentialKind::Admin) {
      // A credential already PROVEN to be an admin token mints straight away — probing
      // it as a session token can only 401 (browser handshake parity). If the server has
      // since stopped accepting it, this lands in the same Expired state as any refusal.
      token_ = token;   // the mint carries the credential as bearer
      int mint = 0;
      const QByteArray minted =
          request("POST", "/auth/token", "{}", "application/json", mint);
      if (mint < 200 || mint >= 300) {
        token_.clear();
        return failAuth();
      }
      token_ = QJsonDocument::fromJson(minted).object().value("token").toString();
      if (token_.isEmpty()) return fail("server returned no token");
      kind_ = CredentialKind::Admin;
    } else {
      token_ = token;
      request("GET", "/projects", {}, {}, status);
      if (status >= 200 && status < 300) {
        kind_ = CredentialKind::Session;   // the token IS a session token
      } else {
        // Not a session token — but it may be the server's ADMIN token (the gate
        // operators hold): try minting a session WITH it. Entering ADMIN_TOKEN in
        // the Token field then just works, instead of a bare 401.
        int mint = 0;
        const QByteArray minted =
            request("POST", "/auth/token", "{}", "application/json", mint);
        if (mint >= 200 && mint < 300) {
          token_ = QJsonDocument::fromJson(minted).object().value("token").toString();
          if (token_.isEmpty()) return fail("server returned no token");
          kind_ = CredentialKind::Admin;   // it minted: an admin credential
        } else {
          token_.clear();
          return failAuth();
        }
      }
    }
    status_ = Status::Connected;
    return true;
  }

  // ── Async REST surface (mirrors the synchronous methods above op-for-op) ──

  void ServerClient::connectAsync(const QString& token, std::function<void(bool)> done,
                                  CredentialKind hint) {
    credential_ = token;
    kind_ = CredentialKind::None;   // re-proven below by whichever path gets in
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
                       const bool refused = status == 401 || status == 403;
                       if (refused)
                         err_ = QStringLiteral("this server gates token minting (ADMIN_TOKEN) — paste a "
                                               "session token, or the admin token, into the Token field");
                       // A gate is a credential problem, not a dead server.
                       status_ = refused ? Status::Expired : Status::Error;
                       if (refused)
                         qWarning("stencil: %s needs a token (ADMIN_TOKEN gate)", qPrintable(base_));
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
    } else if (hint == CredentialKind::Admin) {
      // Known admin credential: mint straight away, no doomed probe (sync-path parity).
      token_ = token;
      requestAsync("POST", "/auth/token", "{}", "application/json",
                   [this, done = std::move(done)](int mint, QByteArray body) {
                     if (mint < 200 || mint >= 300) {
                       token_.clear();
                       status_ = Status::Expired;
                       qWarning("stencil: admin token refused by %s — reconnect to sign in again",
                                qPrintable(base_));
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
                     kind_ = CredentialKind::Admin;
                     status_ = Status::Connected;
                     done(true);
                   });
    } else {
      token_ = token;
      requestAsync("GET", "/projects", {}, {},
                   [this, done = std::move(done)](int status, QByteArray) mutable {
                     if (status >= 200 && status < 300) {
                       kind_ = CredentialKind::Session;   // the token IS a session token
                       status_ = Status::Connected;
                       done(true);
                       return;
                     }
                     // Same admin-token fallback as the sync path: token_ still holds
                     // the entered value, so the mint request carries it as bearer.
                     requestAsync("POST", "/auth/token", "{}", "application/json",
                                  [this, status, done = std::move(done)](int mint, QByteArray body) {
                                    if (mint >= 200 && mint < 300) {
                                      token_ = QJsonDocument::fromJson(body).object().value("token").toString();
                                      if (!token_.isEmpty()) {
                                        kind_ = CredentialKind::Admin;  // it minted: admin
                                        status_ = Status::Connected;
                                        done(true);
                                        return;
                                      }
                                      err_ = "server returned no token";
                                      token_.clear();
                                      status_ = Status::Error;
                                      done(false);
                                      return;
                                    }
                                    // The token is not a session token and not the
                                    // admin token: a refused CREDENTIAL, so the row
                                    // offers a sign-in rather than a dead server.
                                    token_.clear();
                                    status_ = Status::Expired;
                                    qWarning("stencil: token refused by %s — reconnect to sign in again",
                                             qPrintable(base_));
                                    done(false);
                                  });
                   });
    }
  }

  void ServerClient::reconnectAsync(std::function<void(bool)> done) {
    // The CREDENTIAL is what outlives a server restart — reconnect with it when one
    // exists (connectAsync probes it, then mint-falls-back). Reconnecting with the
    // minted session token instead would also overwrite credential_ with it.
    if (!credential_.isEmpty()) {
      // A REUSED credential keeps what we learned about it, so a known admin one
      // never probes again (browser reconnectOne parity).
      connectAsync(credential_, std::move(done), kind_);
      return;
    }
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
                     p.description = o.value("description").toString();
                     p.blankColor = o.value("blankColor").toString();
                     for (const QJsonValue& kv : o.value("keywords").toArray())
                       if (!kv.toString().isEmpty()) p.keywords << kv.toString();
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
                     done(false, meta, layoutOut);
                     return;
                   }
                   const QJsonObject root = QJsonDocument::fromJson(body).object();
                   const QJsonObject p = root.value("project").toObject();
                   meta.id = p.value("id").toString();
                   meta.name = p.value("name").toString();
                   meta.color = p.value("color").toString();
                   meta.description = p.value("description").toString();
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
                   done(status >= 200 && status < 300, data);
                 });
  }

  void ServerClient::deleteFileAsync(const QString& id, const QString& kind,
                                     std::function<void(bool)> done) {
    // Filestore-only kinds (video/variantN/chat) only; the server answers an
    // idempotent 204 (llm-contract.md §9) and refuses original/result.
    requestAsync("DELETE", QString("/projects/%1/files/%2").arg(id, kind), {}, {},
                 [this, done = std::move(done)](int status, QByteArray) {
                   if (status < 200 || status >= 300) {
                     done(false);
                     return;
                   }
                   done(true);
                 });
  }

  void ServerClient::mintInviteAsync(std::function<void(bool, QString)> done) {
    if (credential_.isEmpty()) {
      err_ = "no credential to mint an invite with";
      done(false, QString());
      return;
    }
    // The mint carries the CREDENTIAL as bearer (never the session token) so the
    // invited session outlives this one; token_ stays untouched throughout.
    QNetworkRequest req = buildRequest("/auth/token", "application/json", credential_);
    QNetworkReply* reply =
        nam_->sendCustomRequest(req, "POST", QByteArray("{\"label\":\"invite\"}"));
    QObject::connect(reply, &QNetworkReply::finished, nam_,
                     [this, reply, done = std::move(done)] {
                       const int status =
                           reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                       const QByteArray body = reply->readAll();
                       const QString tok =
                           QJsonDocument::fromJson(body).object().value("token").toString();
                       const QString transport = reply->error() == QNetworkReply::NoError
                                                     ? QString()
                                                     : reply->errorString();
                       reply->deleteLater();
                       if (status < 200 || status >= 300 || tok.isEmpty()) {
                         err_ = restError("POST", "/auth/token", status, body, transport);
                         done(false, QString());
                         return;
                       }
                       done(true, inviteLink(base_, tok));
                     });
  }

  void ServerClient::deleteProjectAsync(const QString& id, std::function<void(bool)> done) {
    requestAsync("DELETE", QString("/projects/%1").arg(id), {}, {},
                 [this, done = std::move(done)](int status, QByteArray) {
                   if (status < 200 || status >= 300) {
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

  bool ConnectionManager::connectTo(const QString& url, const QString& token, QString& err,
                                    ServerClient::CredentialKind kindHint) {
    // Invite link: a "#token=<tok>" fragment supplies the credential — split it off
    // before normalization (which drops fragments). An explicitly-typed token wins.
    QString linkToken;
    const QString stripped = ServerClient::splitInviteToken(url, linkToken);
    const QString cred = token.isEmpty() ? linkToken : token;
    const QString base = ServerClient::normalizeBase(stripped);
    if (find(base)) {
      err = "already connected";
      return false;
    }
    auto* client = new ServerClient(base);
    // The hint is only ever supplied by a caller REUSING a proven credential (the saved
    // set); a freshly typed or invite-link token arrives without one and probes first.
    if (!client->connect(cred, kindHint)) {
      err = client->lastError();
      // A REFUSED CREDENTIAL keeps its place: the server is fine and the URL worth
      // keeping, so the row can offer a sign-in. An unreachable host is still
      // dropped — there is nothing to sign in to.
      if (client->needsReauth()) {
        clients_.push_back(client);
        emit changed();
        return false;
      }
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

  void ConnectionManager::reorder(int from, int to) {
    if (from < 0 || from >= clients_.size()) return;
    if (to < 0) to = 0;
    if (to >= clients_.size()) to = clients_.size() - 1;
    if (from == to) return;
    clients_.move(from, to);
    emit changed();
  }

  bool ConnectionManager::reauthenticate(const QString& url, const QString& token,
                                        QString& err) {
    ServerClient* c = find(url);
    if (!c) return connectTo(url, token, err);   // nothing listed: an ordinary connect
    // The client is REUSED, credential and all (ServerClient::connect re-proves the kind),
    // so the row keeps its place and its identity. connectTo() answered "already connected"
    // and left the session expired however good the pasted token was (user report).
    if (!c->connect(token)) {
      err = c->lastError();
      emit changed();
      return false;
    }
    emit changed();
    return true;
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

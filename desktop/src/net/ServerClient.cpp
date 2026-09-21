#include "ServerClient.hpp"

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
      : nam(new QNetworkAccessManager), base(normalizeBase(url)) {}

  ServerClient::~ServerClient() { delete nam; }

  bool ServerClient::isLoopbackHost(const QString& host) {
    if (host.isEmpty()) return false;
    if (host.compare("localhost", Qt::CaseInsensitive) == 0) return true;
    if (host.endsWith(".localhost", Qt::CaseInsensitive)) return true;
    const QHostAddress addr(host);
    return !addr.isNull() && addr.isLoopback();
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
      // Bare host gets https, EXCEPT loopback (dev servers speak plaintext and never leave the machine).
      const QString host = QUrl("http://" + s).host();
      s = (isLoopbackHost(host) ? QStringLiteral("http://") : QStringLiteral("https://")) + s;
    }
    QUrl u(s);
    QString origin = u.scheme() + "://" + u.authority();
    return origin;
  }

  QString ServerClient::splitInviteToken(const QString& raw, QString& token) {
    token.clear();
    const int hash = raw.indexOf('#');
    if (hash < 0) return raw;
    const QString frag = raw.mid(hash + 1);
    if (!frag.startsWith(QLatin1String("token="))) return raw;
    token = QUrl::fromPercentEncoding(frag.mid(6).toUtf8());
    return raw.left(hash);
  }

  QString ServerClient::inviteLink(const QString& base, const QString& token) {
    return base + "#token=" + token;
  }

  QString ServerClient::kindTag(CredentialKind k) {
    return k == CredentialKind::ADMIN     ? QStringLiteral("admin")
           : k == CredentialKind::SESSION ? QStringLiteral("session")
                                          : QString();
  }

  ServerClient::CredentialKind ServerClient::kindFromTag(const QString& tag) {
    if (tag == QLatin1String("admin")) return CredentialKind::ADMIN;
    if (tag == QLatin1String("session")) return CredentialKind::SESSION;
    return CredentialKind::NONE;
  }

  namespace {
    // Browser parity (net/connectionManager.js _req): "<METHOD> <path>: <why>", <why> = the server's JSON
    // `message` or "HTTP <status>"; a request that never reached the server shows the transport's message, never "HTTP 0".
    QString restError(const QByteArray& method, const QString& path, int status,
                             const QByteArray& body, const QString& transport) {
      if (status == 0)
        return transport.isEmpty() ? QStringLiteral("the request never reached the server")
                                   : transport;
      QString why = QJsonDocument::fromJson(body).object().value("message").toString();
      if (why.isEmpty()) why = QStringLiteral("HTTP %1").arg(status);
      const int q = path.indexOf('?');   // the browser reports the path, never its query
      return QString("%1 %2: %3").arg(QLatin1String(method), q < 0 ? path : path.left(q), why);
    }
  }  // namespace

  QNetworkRequest ServerClient::buildRequest(const QString& path,
                                             const QString& contentType,
                                             const QString& bearer) const {
    QNetworkRequest req{QUrl(base + path)};
    // Bounded so a hung/malicious server cannot wedge a transfer forever.
    req.setTransferTimeout(20000);
    const QString& tok = bearer.isEmpty() ? token : bearer;
    if (!tok.isEmpty())
      req.setRawHeader("Authorization", "Bearer " + tok.toUtf8());
    if (!contentType.isEmpty())
      req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    return req;
  }

  void ServerClient::requestAsync(const QByteArray& method, const QString& path,
                                  const QByteArray& body, const QString& contentType,
                                  std::function<void(int status, QByteArray body)> done,
                                  bool retried) {
    QNetworkRequest req = buildRequest(path, contentType);
    QNetworkReply* reply = nam->sendCustomRequest(req, method, body);
    // Context object is nam (owned by this client): when the client dies nam goes with it, the
    // connection is severed and this slot never runs on a dangling `this`.
    QObject::connect(reply, &QNetworkReply::finished, nam,
                     [this, reply, method, path, body, contentType, retried,
                      done = std::move(done)]() mutable {
                       const int status =
                           reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                       const QByteArray data = reply->readAll();
                       if (!isOkStatus(status))
                         err = restError(method, path, status, data,
                                          reply->error() == QNetworkReply::NoError
                                              ? QString()
                                              : reply->errorString());
                       reply->deleteLater();
                       const bool refused = isAuthStatus(status);
                       // A minted session dies with a server restart — re-mint with the credential once and retry in place.
                       if (refused && this->status == Status::CONNECTED && !retried &&
                           !credential.isEmpty() && path != QLatin1String("/auth/token")) {
                         token = credential;
                         requestAsync("POST", "/auth/token", "{}", "application/json",
                                      [this, method, path, body, contentType,
                                       done = std::move(done)](int mint, QByteArray mb) mutable {
                                        const QString tok = QJsonDocument::fromJson(mb)
                                                                .object().value("token").toString();
                                        if (!isOkStatus(mint) || tok.isEmpty()) {
                                          done(mint, {});
                                          return;
                                        }
                                        token = tok;
                                        requestAsync(
                                            method, path, body, contentType,
                                            [this, done = std::move(done)](int st, QByteArray rb) {
                                              // It minted AND the session works: an admin token (browser parity).
                                              if (isOkStatus(st))
                                                kind = CredentialKind::ADMIN;
                                              done(st, rb);
                                            },
                                            /*retried=*/true);
                                      },
                                      /*retried=*/true);
                         return;
                       }
                       // Refused mid-flight and past rescue is EXPIRED, not a dead server. One warning, on the way in.
                       if (refused && this->status == Status::CONNECTED) {
                         this->status = Status::EXPIRED;
                         qWarning("stencil: session on %s expired — reconnect to sign in again",
                                  qPrintable(base));
                       }
                       done(status, data);
                     });
  }

  void ServerClient::mintInviteAsync(std::function<void(bool, QString)> done) {
    if (credential.isEmpty()) {
      err = "no credential to mint an invite with";
      done(false, QString());
      return;
    }
    // The mint carries the CREDENTIAL as bearer so the invited session outlives this one.
    QNetworkRequest req = buildRequest("/auth/token", "application/json", credential);
    QNetworkReply* reply =
        nam->sendCustomRequest(req, "POST", QByteArray("{\"label\":\"invite\"}"));
    QObject::connect(reply, &QNetworkReply::finished, nam,
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
                       if (!isOkStatus(status) || tok.isEmpty()) {
                         err = restError("POST", "/auth/token", status, body, transport);
                         done(false, QString());
                         return;
                       }
                       done(true, inviteLink(base, tok));
                     });
  }
}  // namespace stencil::net


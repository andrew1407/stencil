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
}  // namespace stencil::net


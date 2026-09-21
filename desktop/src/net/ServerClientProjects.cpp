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


  void ServerClient::connectAsync(const QString& token, std::function<void(bool)> done,
                                  CredentialKind hint) {
    credential = token;
    kind = CredentialKind::NONE;   // re-proven below by whichever path gets in
    status = Status::CONNECTING;
    if (base.isEmpty()) {
      err = "empty server URL";
      status = Status::ERROR;
      done(false);
      return;
    }
    if (token.isEmpty()) {
      requestAsync("POST", "/auth/token", "{}", "application/json",
                   [this, done = std::move(done)](int status, QByteArray body) {
                     if (!isOkStatus(status)) {
                       const bool refused = isAuthStatus(status);
                       if (refused)
                         err = QStringLiteral("this server gates token minting (ADMIN_TOKEN) — paste a "
                                               "session token, or the admin token, into the Token field");
                       this->status = refused ? Status::EXPIRED : Status::ERROR;
                       if (refused)
                         qWarning("stencil: %s needs a token (ADMIN_TOKEN gate)", qPrintable(base));
                       done(false);
                       return;
                     }
                     this->token = QJsonDocument::fromJson(body).object().value("token").toString();
                     if (this->token.isEmpty()) {
                       err = "server returned no token";
                       this->status = Status::ERROR;
                       done(false);
                       return;
                     }
                     this->status = Status::CONNECTED;
                     done(true);
                   });
    } else if (hint == CredentialKind::ADMIN) {
      // Known admin credential: mint straight away, no doomed probe (sync-path parity).
      this->token = token;
      requestAsync("POST", "/auth/token", "{}", "application/json",
                   [this, done = std::move(done)](int mint, QByteArray body) {
                     if (!isOkStatus(mint)) {
                       this->token.clear();
                       this->status = Status::EXPIRED;
                       qWarning("stencil: admin token refused by %s — reconnect to sign in again",
                                qPrintable(base));
                       done(false);
                       return;
                     }
                     this->token = QJsonDocument::fromJson(body).object().value("token").toString();
                     if (this->token.isEmpty()) {
                       err = "server returned no token";
                       this->status = Status::ERROR;
                       done(false);
                       return;
                     }
                     kind = CredentialKind::ADMIN;
                     this->status = Status::CONNECTED;
                     done(true);
                   });
    } else {
      this->token = token;
      requestAsync("GET", "/projects", {}, {},
                   [this, done = std::move(done)](int status, QByteArray) mutable {
                     if (isOkStatus(status)) {
                       kind = CredentialKind::SESSION;   // the token IS a session token
                       this->status = Status::CONNECTED;
                       done(true);
                       return;
                     }
                     requestAsync("POST", "/auth/token", "{}", "application/json",
                                  [this, status, done = std::move(done)](int mint, QByteArray body) {
                                    if (isOkStatus(mint)) {
                                      this->token = QJsonDocument::fromJson(body).object().value("token").toString();
                                      if (!this->token.isEmpty()) {
                                        kind = CredentialKind::ADMIN;  // it minted: admin
                                        this->status = Status::CONNECTED;
                                        done(true);
                                        return;
                                      }
                                      err = "server returned no token";
                                      this->token.clear();
                                      this->status = Status::ERROR;
                                      done(false);
                                      return;
                                    }
                                    // Neither a session nor the admin token: a refused CREDENTIAL, so the row offers a sign-in.
                                    this->token.clear();
                                    this->status = Status::EXPIRED;
                                    qWarning("stencil: token refused by %s — reconnect to sign in again",
                                             qPrintable(base));
                                    done(false);
                                  });
                   });
    }
  }

  void ServerClient::reconnectAsync(std::function<void(bool)> done) {
    // Reconnect with the CREDENTIAL (it outlives a restart); the session token would overwrite credential.
    if (!credential.isEmpty()) {
      // A REUSED credential keeps its kind (browser reconnectOne parity).
      connectAsync(credential, std::move(done), kind);
      return;
    }
    if (token.isEmpty()) {
      connectAsync(QString(), std::move(done));
      return;
    }
    connectAsync(token, [this, done](bool ok) {
      if (ok) {
        done(true);
        return;
      }
      token.clear();
      connectAsync(QString(), done);
    });
  }

  void ServerClient::listProjectsAsync(
      std::function<void(bool, QVector<ServerProject>)> done) {
    requestAsync("GET", "/projects", {}, {},
                 [this, done = std::move(done)](int status, QByteArray body) {
                   QVector<ServerProject> out;
                   if (!isOkStatus(status)) {
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
                     p.serverUrl = base;
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
                   if (!isOkStatus(status)) {
                     done(false, QString(), 0);
                     return;
                   }
                   const QJsonObject rec = QJsonDocument::fromJson(body).object();
                   const QString id = rec.value("id").toString();
                   const qint64 ver = static_cast<qint64>(rec.value("version").toDouble());
                   done(!id.isEmpty(), id, ver);
                 });
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
}  // namespace stencil::net


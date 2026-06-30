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

  QByteArray ServerClient::request(const QByteArray& method, const QString& path,
                                   const QByteArray& body, const QString& contentType,
                                   int& status) {
    status = 0;
    QNetworkRequest req{QUrl(base_ + path)};
    // Bound every request so a hung/malicious server can't freeze the UI thread (the call
    // below blocks on a nested event loop); the reply then finishes with a timeout error.
    req.setTransferTimeout(20000);
    if (!token_.isEmpty())
      req.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    if (!contentType.isEmpty())
      req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);

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

  bool ServerClient::reconnect() {
    // Re-validate the token we already hold; a still-valid one keeps working
    // without a fresh handshake. If it's been rejected (cleared by connect()) or
    // we never had one, fall back to issuing a brand-new token.
    if (!token_.isEmpty() && connect(token_)) return true;
    token_.clear();
    return connect();
  }

  bool ServerClient::listProjects(QVector<ServerProject>& out) {
    int status = 0;
    const QByteArray body = request("GET", "/projects", {}, {}, status);
    if (status < 200 || status >= 300) {
      err_ = QString("list failed (HTTP %1)").arg(status);
      return false;
    }
    out.clear();
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
      p.updatedAt = static_cast<qint64>(o.value("updatedAt").toDouble());
      p.version = static_cast<qint64>(o.value("version").toDouble());
      p.serverUrl = base_;
      out.push_back(p);
    }
    return true;
  }

  bool ServerClient::createProject(const QString& name, const QString& source,
                                   const QString& resource, bool hasImage, int w, int h,
                                   QString& outId, qint64& outVersion) {
    QJsonObject obj;
    obj.insert("name", name);
    obj.insert("source", source);
    obj.insert("resource", resource);
    obj.insert("hasImage", hasImage);
    obj.insert("imageW", w);
    obj.insert("imageH", h);
    int status = 0;
    const QByteArray body = request("POST", "/projects",
                                    QJsonDocument(obj).toJson(QJsonDocument::Compact),
                                    "application/json", status);
    if (status < 200 || status >= 300) {
      err_ = QString("create failed (HTTP %1)").arg(status);
      return false;
    }
    const QJsonObject rec = QJsonDocument::fromJson(body).object();
    outId = rec.value("id").toString();
    outVersion = static_cast<qint64>(rec.value("version").toDouble());
    return !outId.isEmpty();
  }

  bool ServerClient::getProject(const QString& id, ServerProject& meta,
                                QJsonObject& layoutOut) {
    int status = 0;
    const QByteArray body =
        request("GET", QString("/projects/%1").arg(id), {}, {}, status);
    if (status < 200 || status >= 300) {
      err_ = QString("get failed (HTTP %1)").arg(status);
      return false;
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
    // The layout rides alongside the record (handleGetProject nulls project.layout
    // and lifts it to the top level).
    layoutOut = root.value("layout").toObject();
    return true;
  }

  bool ServerClient::updateProject(const QString& id, const QString& name,
                                   const QJsonObject& layout, qint64 version,
                                   qint64& newVersion, bool& conflict) {
    conflict = false;
    QJsonObject obj;
    if (!name.isEmpty()) obj.insert("name", name);
    obj.insert("layout", layout);
    obj.insert("version", static_cast<double>(version));
    int status = 0;
    const QByteArray body = request("PUT", QString("/projects/%1").arg(id),
                                    QJsonDocument(obj).toJson(QJsonDocument::Compact),
                                    "application/json", status);
    if (status == 409) {
      conflict = true;
      err_ = "stale version (edited elsewhere)";
      return false;
    }
    if (status < 200 || status >= 300) {
      err_ = QString("update failed (HTTP %1)").arg(status);
      return false;
    }
    newVersion =
        static_cast<qint64>(QJsonDocument::fromJson(body).object().value("version").toDouble());
    return true;
  }

  bool ServerClient::updateProjectColor(const QString& id, const QString& color,
                                        qint64 version, qint64& newVersion, bool& conflict) {
    conflict = false;
    QJsonObject obj;
    // Always send `color` (even ""), so an explicit clear reaches the server; name +
    // layout are omitted, so the server's COALESCE leaves them unchanged.
    obj.insert("color", color);
    obj.insert("version", static_cast<double>(version));
    int status = 0;
    const QByteArray body = request("PUT", QString("/projects/%1").arg(id),
                                    QJsonDocument(obj).toJson(QJsonDocument::Compact),
                                    "application/json", status);
    if (status == 409) {
      conflict = true;
      err_ = "stale version (edited elsewhere)";
      return false;
    }
    if (status < 200 || status >= 300) {
      err_ = QString("update failed (HTTP %1)").arg(status);
      return false;
    }
    newVersion =
        static_cast<qint64>(QJsonDocument::fromJson(body).object().value("version").toDouble());
    return true;
  }

  bool ServerClient::updateProjectName(const QString& id, const QString& name,
                                       qint64 version, qint64& newVersion, bool& conflict) {
    conflict = false;
    QJsonObject obj;
    obj.insert("name", name);  // colour + layout omitted → the server's COALESCE leaves them
    obj.insert("version", static_cast<double>(version));
    int status = 0;
    const QByteArray body = request("PUT", QString("/projects/%1").arg(id),
                                    QJsonDocument(obj).toJson(QJsonDocument::Compact),
                                    "application/json", status);
    if (status == 409) {
      conflict = true;
      err_ = "stale version (edited elsewhere)";
      return false;
    }
    if (status < 200 || status >= 300) {
      err_ = QString("rename failed (HTTP %1)").arg(status);
      return false;
    }
    newVersion =
        static_cast<qint64>(QJsonDocument::fromJson(body).object().value("version").toDouble());
    return true;
  }

  bool ServerClient::uploadFile(const QString& id, const QString& kind,
                                const QByteArray& bytes, const QString& ext, int w, int h) {
    QString path = QString("/projects/%1/files/%2").arg(id, kind);
    QUrlQuery q;
    q.addQueryItem("ext", ext);
    q.addQueryItem("w", QString::number(w));
    q.addQueryItem("h", QString::number(h));
    path += "?" + q.toString(QUrl::FullyEncoded);
    int status = 0;
    request("POST", path, bytes, "application/octet-stream", status);
    if (status < 200 || status >= 300) {
      err_ = QString("upload failed (HTTP %1)").arg(status);
      return false;
    }
    return true;
  }

  QByteArray ServerClient::downloadFile(const QString& id, const QString& kind, bool& ok) {
    int status = 0;
    const QByteArray data =
        request("GET", QString("/projects/%1/files/%2").arg(id, kind), {}, {}, status);
    ok = status >= 200 && status < 300;
    if (!ok) err_ = QString("download failed (HTTP %1)").arg(status);
    return data;
  }

  bool ServerClient::deleteProject(const QString& id) {
    int status = 0;
    request("DELETE", QString("/projects/%1").arg(id), {}, {}, status);
    if (status < 200 || status >= 300) {
      err_ = QString("delete failed (HTTP %1)").arg(status);
      return false;
    }
    return true;
  }

  // ── ConnectionManager ──

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

  bool ConnectionManager::reconnect(const QString& url, QString& err) {
    ServerClient* c = find(url);
    if (!c) {
      err = "not connected";
      return false;
    }
    if (!c->reconnect()) {
      err = c->lastError();
      emit changed();  // a now-dead connection still warrants a UI refresh
      return false;
    }
    emit changed();
    return true;
  }

  void ConnectionManager::reconnectAll() {
    for (auto* c : clients_) c->reconnect();
    emit changed();
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

  QVector<ServerProject> ConnectionManager::sharedProjects() const {
    QVector<ServerProject> out;
    for (auto* c : clients_) {
      QVector<ServerProject> ps;
      if (c->listProjects(ps)) {
        for (const auto& p : ps)
          if (p.hasImage) out.push_back(p);
      }
    }
    return out;
  }

}  // namespace stencil::net

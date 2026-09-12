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
}  // namespace stencil::net


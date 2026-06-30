#include "liveFeed.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace stencil::net {

  LiveFeed::LiveFeed(QObject* parent) : QObject(parent) {
    // A stable per-feed id for the hello frame (the server only relays it back as
    // fromClientId; we never read it). Mirrors the CLI/browser client-id contract.
    clientId_ = QStringLiteral("desktop-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
  }

  LiveFeed::~LiveFeed() { unsubscribe(); }

  bool LiveFeed::subscribe(const QString& base, const QString& token) {
    // The plaintext feed can't ride TLS — decline an https origin and let the poll
    // backstop cover peer changes there (mirrors the CLI's EditConn.open).
    if (base.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
      unsubscribe();
      return false;
    }
    // Already pointed at this origin — keep the live socket, just refresh the token.
    if (base_ == base && sock_) {
      token_ = token;
      return true;
    }
    unsubscribe();
    const QUrl u(base);
    host_ = u.host();
    if (host_.isEmpty()) return false;
    port_ = static_cast<quint16>(u.port(80) + 1);  // edit channel = REST port + 1
    base_ = base;
    token_ = token;
    dial();
    return true;
  }

  void LiveFeed::unsubscribe() {
    if (retry_) retry_->stop();
    if (sock_) {
      sock_->disconnect(this);  // silence our slots during teardown (no reconnect)
      sock_->abort();
      sock_->deleteLater();
      sock_ = nullptr;
    }
    rbuf_.clear();
    base_.clear();
    host_.clear();
    port_ = 0;
    token_.clear();
  }

  void LiveFeed::dial() {
    if (host_.isEmpty() || port_ == 0) return;
    if (!sock_) {
      sock_ = new QTcpSocket(this);
      connect(sock_, &QTcpSocket::connected, this, &LiveFeed::onConnected);
      connect(sock_, &QTcpSocket::readyRead, this, &LiveFeed::onReadyRead);
      connect(sock_, &QTcpSocket::errorOccurred, this, &LiveFeed::onError);
      connect(sock_, &QTcpSocket::disconnected, this, &LiveFeed::onError);
    }
    rbuf_.clear();
    sock_->connectToHost(host_, port_);
  }

  void LiveFeed::onConnected() {
    if (!sock_) return;
    // An empty projectId selects the global events feed (hub.serveEvents). Compact JSON
    // + '\n' is the NDJSON frame the TCP edit channel expects.
    const QJsonObject hello{
        {QLatin1String("type"), QLatin1String("hello")},
        {QLatin1String("token"), token_},
        {QLatin1String("projectId"), QString()},
        {QLatin1String("clientId"), clientId_},
    };
    sock_->write(QJsonDocument(hello).toJson(QJsonDocument::Compact) + '\n');
  }

  // A project-event frame is tiny (id + version); anything past this with no newline is a
  // misbehaving/hostile peer streaming bytes unboundedly. Cap the buffer to bound memory.
  static constexpr int kMaxBufferBytes = 1 << 20;  // 1 MiB

  void LiveFeed::onReadyRead() {
    if (!sock_) return;
    rbuf_ += sock_->readAll();
    // No frame delimiter within the cap → drop the abusive stream rather than grow without
    // bound. abort() trips onError(), which schedules a reconnect; the poll backstop covers
    // the gap. (A well-behaved feed never approaches this — its frames are well under 1 KiB.)
    if (rbuf_.size() > kMaxBufferBytes && !rbuf_.contains('\n')) {
      rbuf_.clear();
      if (sock_) sock_->abort();
      onError();
      return;
    }
    parseFrames();
  }

  void LiveFeed::parseFrames() {
    int nl;
    while ((nl = rbuf_.indexOf('\n')) >= 0) {
      const QByteArray line = rbuf_.left(nl);
      rbuf_.remove(0, nl + 1);
      if (line.trimmed().isEmpty()) continue;
      const QJsonDocument doc = QJsonDocument::fromJson(line);
      if (!doc.isObject()) continue;  // skip welcome/synced/other frames
      const QJsonObject o = doc.object();
      if (o.value(QLatin1String("type")).toString() != QLatin1String("project-event")) continue;
      const QJsonObject proj = o.value(QLatin1String("project")).toObject();
      const QString id = proj.value(QLatin1String("id")).toString();
      if (id.isEmpty()) continue;
      const qint64 version = static_cast<qint64>(proj.value(QLatin1String("version")).toDouble());
      const bool deleted = o.value(QLatin1String("event")).toString() == QLatin1String("deleted");
      emit projectUpdated(id, version, deleted);
    }
  }

  void LiveFeed::onError() {
    // A drop/refusal while we still have a target: schedule one reconnect. The poll
    // backstop covers the gap until it lands; unsubscribe() clears base_ so this stops.
    if (base_.isEmpty()) return;
    if (!retry_) {
      retry_ = new QTimer(this);
      retry_->setSingleShot(true);
      connect(retry_, &QTimer::timeout, this, [this] {
        if (!base_.isEmpty()) dial();
      });
    }
    if (!retry_->isActive()) retry_->start(3000);
  }

}  // namespace stencil::net

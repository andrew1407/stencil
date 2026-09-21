#include "LiveFeed.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace stencil::net {

  LiveFeed::LiveFeed(QObject* parent) : QObject(parent) {
    // The server only relays this id back as fromClientId; mirrors the CLI/browser client-id contract.
    clientId = QStringLiteral("desktop-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
  }

  LiveFeed::~LiveFeed() { unsubscribe(); }

  bool LiveFeed::subscribe(const QString& base, const QString& token) {
    // The plaintext feed cannot ride TLS; the poll backstop covers https (CLI EditConn.open parity).
    if (base.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
      unsubscribe();
      return false;
    }
    if (this->base == base && sock) {
      this->token = token;
      return true;
    }
    unsubscribe();
    const QUrl u(base);
    host = u.host();
    if (host.isEmpty()) return false;
    port = static_cast<quint16>(u.port(80) + 1);  // edit channel = REST port + 1
    this->base = base;
    this->token = token;
    dial();
    return true;
  }

  void LiveFeed::unsubscribe() {
    if (retry) retry->stop();
    if (sock) {
      sock->disconnect(this);  // silence our slots during teardown (no reconnect)
      sock->abort();
      sock->deleteLater();
      sock = nullptr;
    }
    rbuf.clear();
    base.clear();
    host.clear();
    port = 0;
    token.clear();
  }

  void LiveFeed::dial() {
    if (host.isEmpty() || port == 0) return;
    if (!sock) {
      sock = new QTcpSocket(this);
      connect(sock, &QTcpSocket::connected, this, &LiveFeed::onConnected);
      connect(sock, &QTcpSocket::readyRead, this, &LiveFeed::onReadyRead);
      connect(sock, &QTcpSocket::errorOccurred, this, &LiveFeed::onError);
      connect(sock, &QTcpSocket::disconnected, this, &LiveFeed::onError);
    }
    rbuf.clear();
    sock->connectToHost(host, port);
  }

  void LiveFeed::onConnected() {
    if (!sock) return;
    // An empty projectId selects the global events feed (hub.serveEvents).
    const QJsonObject hello{
        {QLatin1String("type"), QLatin1String("hello")},
        {QLatin1String("token"), token},
        {QLatin1String("projectId"), QString()},
        {QLatin1String("clientId"), clientId},
    };
    sock->write(QJsonDocument(hello).toJson(QJsonDocument::Compact) + '\n');
  }

  namespace {
    // A frame is tiny (id + version); a longer newline-less stream is a hostile peer.
    constexpr int MAX_BUFFER_BYTES = 1 << 20;  // 1 MiB
  }  // namespace

  void LiveFeed::onReadyRead() {
    if (!sock) return;
    rbuf += sock->readAll();
    // abort() trips onError(), which schedules a reconnect; the poll backstop covers the gap.
    if (rbuf.size() > MAX_BUFFER_BYTES && !rbuf.contains('\n')) {
      rbuf.clear();
      if (sock) sock->abort();
      onError();
      return;
    }
    parseFrames();
  }

  void LiveFeed::parseFrames() {
    int nl;
    while ((nl = rbuf.indexOf('\n')) >= 0) {
      const QByteArray line = rbuf.left(nl);
      rbuf.remove(0, nl + 1);
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
    // One reconnect per drop; unsubscribe() clears base so this stops.
    if (base.isEmpty()) return;
    if (!retry) {
      retry = new QTimer(this);
      retry->setSingleShot(true);
      connect(retry, &QTimer::timeout, this, [this] {
        if (!base.isEmpty()) dial();
      });
    }
    if (!retry->isActive()) retry->start(3000);
  }

}  // namespace stencil::net

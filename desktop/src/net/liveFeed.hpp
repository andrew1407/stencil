#pragma once
// ── Collaboration-server live events feed (desktop) ─────────────────────────
// A read-only subscription to a server's GLOBAL project-events feed over the raw-TCP
// edit channel (REST port + 1). It connects, sends a hello with an empty projectId —
// which the server routes to the events feed (see hub.serveEvents) — and emits
// projectUpdated() for each "project-event" NDJSON frame. This is the desktop's push
// replacement for version polling: a peer's save reaches us in tens of ms instead of
// on the next poll tick. Plaintext only — an https server's edit channel is TLS-wrapped,
// so subscribe() declines an https base and the poll backstop covers it. Mirrors the
// CLI's EditConn (cli/src/server.zig) and the browser's WebSocket feed.
#include <QByteArray>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

namespace stencil::net {

  class LiveFeed : public QObject {
    Q_OBJECT
   public:
    explicit LiveFeed(QObject* parent = nullptr);
    ~LiveFeed() override;

    // (Re)subscribe to `base` (a normalized origin like "http://host:8090"),
    // authenticating with `token`. A no-op (token refresh only) when already pointed at
    // the same base. Returns false — and tears down any prior subscription — for an
    // https base, which can't speak the plaintext feed.
    bool subscribe(const QString& base, const QString& token);
    // Drop the subscription and close the socket. Safe to call when not subscribed.
    void unsubscribe();
    // The origin currently subscribed to (empty when not subscribed).
    const QString& base() const { return base_; }

   signals:
    // A project changed on the server (id + new monotonic version). `deleted` marks a
    // delete event rather than an edit.
    void projectUpdated(const QString& id, qint64 version, bool deleted);

   private slots:
    void onConnected();
    void onReadyRead();
    void onError();

   private:
    void dial();         // open the socket to host_:port_ (idempotent)
    void parseFrames();  // pop complete NDJSON lines from rbuf_ and emit events

    QTcpSocket* sock_ = nullptr;
    QTimer* retry_ = nullptr;  // single-shot reconnect after a drop
    QString base_;
    QString host_;
    quint16 port_ = 0;
    QString token_;
    QString clientId_;
    QByteArray rbuf_;
  };

}  // namespace stencil::net

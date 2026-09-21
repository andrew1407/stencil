#pragma once
// Read-only subscription to a server's GLOBAL project-events feed over the raw-TCP edit channel
// (REST port + 1). Plaintext only — an https base is declined and polling covers it.
// Mirrors the CLI's EditConn (cli/src/server.zig).
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

    // A no-op (token refresh only) when already pointed at the same base; returns false for an
    // https base, tearing down any prior subscription.
    bool subscribe(const QString& base, const QString& token);
    void unsubscribe();
    const QString& getBase() const { return base; }

   signals:
    // `deleted` marks a delete event rather than an edit.
    void projectUpdated(const QString& id, qint64 version, bool deleted);

   private slots:
    void onConnected();
    void onReadyRead();
    void onError();

   private:
    void dial();
    void parseFrames();

    QTcpSocket* sock = nullptr;
    QTimer* retry = nullptr;
    QString base;
    QString host;
    quint16 port = 0;
    QString token;
    QString clientId;
    QByteArray rbuf;
  };

}  // namespace stencil::net

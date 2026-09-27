#pragma once
// A local HTTP stand-in that serves one 20x14 PNG per request, but only once the test releases
// it: the way a GUI case catches a plan or a script parked at a download. Fully offline.
#include <QBuffer>
#include <QColor>
#include <QHostAddress>
#include <QImage>
#include <QList>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <functional>
#include <utility>

namespace stencil::test {

  struct HeldImageServer {
    QTcpServer server;
    QByteArray png;
    QList<QPointer<QTcpSocket>> waiting;
    bool hold = true;   // false: every request is answered as it arrives
    int requests = 0;
    std::function<void()> onRequest;

    bool listen() {
      QImage img(20, 14, QImage::Format_RGB32);
      img.fill(QColor("#3366cc"));
      QBuffer buf(&png);
      if (!buf.open(QIODevice::WriteOnly) || !img.save(&buf, "PNG")) return false;
      QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
        QTcpSocket* s = server.nextPendingConnection();
        QObject::connect(s, &QTcpSocket::readyRead, s, [this, s] {
          s->readAll();
          ++requests;
          if (onRequest) onRequest();
          waiting << s;
          if (!hold) release();
        });
        QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
      });
      return server.listen(QHostAddress::LocalHost, 0);
    }

    // Answers everything held so far, and every request from here on.
    void release() {
      hold = false;
      for (const QPointer<QTcpSocket>& s : std::exchange(waiting, {})) {
        if (!s) continue;
        s->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: " +
                 QByteArray::number(png.size()) + "\r\nConnection: close\r\n\r\n" + png);
        s->disconnectFromHost();
      }
    }

    QString url() const {
      return QStringLiteral("http://127.0.0.1:%1/cat.png").arg(server.serverPort());
    }
  };

}  // namespace stencil::test

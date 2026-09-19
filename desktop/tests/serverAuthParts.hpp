#pragma once
// The server-auth suite's sections, one TU each behind this header, called in this order from
// main(). Every one drives the real ConnectionManager against the in-process MockServer.
#include "ConnectDialog.hpp"
#include "connectionStore.hpp"
#include "DisintegrateOverlay.hpp"  // the DESTRUCTIVE effect a filter-out must not use
#include "filterFade.hpp"           // the light filter transition it uses instead
#include "ServerClient.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <cstdio>
#include <functional>

using stencil::gui::ConnectDialog;
using stencil::gui::DisintegrateOverlay;
using stencil::gui::filteredIn;
using stencil::gui::FILTER_FADE_MS;
using stencil::gui::FILTER_FULL_HEIGHT_ROLE;
using stencil::net::ConnectionManager;
using stencil::net::ServerClient;

#include "support/check.hpp"
#include "support/connectNow.hpp"

namespace serverauth {

static void pumpFor(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}
static void pumpUntil(const std::function<bool()>& pred, int timeoutMs = 3000) {
  QElapsedTimer t;
  t.start();
  while (!pred() && t.elapsed() < timeoutMs)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// One mock server whose answer per path is scriptable at runtime.
struct MockServer {
  QTcpServer server;
  int tokenStatus = 200;     // POST /auth/token
  int projectsStatus = 200;  // GET /projects (and everything else)
  QByteArray mintToken = "tok";  // what a successful mint hands out
  QByteArray goodBearer;     // non-empty: non-mint paths 401 unless this bearer is sent
  QByteArray mintBearer;     // non-empty: the mint itself 401s unless this bearer is sent
  int tokenRequests = 0;
  int requests = 0;

  bool listen() {
    QObject::connect(&server, &QTcpServer::newConnection, [this] {
      while (QTcpSocket* s = server.nextPendingConnection()) {
        QObject::connect(s, &QTcpSocket::readyRead, [this, s] {
          const QByteArray head = s->readAll();
          ++requests;
          const bool mint = head.contains("/auth/token");
          if (mint) ++tokenRequests;
          const int status = mint ? (!mintBearer.isEmpty() &&
                                             !head.contains("Bearer " + mintBearer)
                                         ? 401
                                         : tokenStatus)
                             : !goodBearer.isEmpty()
                                 ? (head.contains("Bearer " + goodBearer) ? 200 : 401)
                                 : projectsStatus;
          const QByteArray body = mint ? "{\"token\":\"" + mintToken + "\"}" : QByteArray("[]");
          s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\n"
                   "Content-Type: application/json\r\nContent-Length: " +
                   QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
          s->flush();
          s->disconnectFromHost();
        });
      }
    });
    return server.listen(QHostAddress::LocalHost, 0);
  }
  QString url() const {
    return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
  }
};

  void checkClassification(MockServer& mock, MockServer& mock2);
  void checkCredentialKind(MockServer& mock, MockServer& mock2);
  void checkRemintAndExpiry(MockServer& mock, MockServer& mock2);
  void checkInvites(MockServer& mock, MockServer& mock2);
  void checkAdminRow(MockServer& mock, MockServer& mock2);

}  // namespace serverauth

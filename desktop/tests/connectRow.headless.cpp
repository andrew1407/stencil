// Headless check for the Servers dialog's connection rows (dialogs/connectDialog):
// a long URL elides inside the viewport, each row is a projects-style card whose
// outline is never clipped (and hovers as one), a row the viewport cuts dissolves at
// the edge, removal retires-then-finalizes, and a new row gathers in. The kind
// filter is a question re-answered: what it excludes is gone at once with nothing to
// watch, the rows that are LEFT arrive, none of the removal's dust is spent, and reduced
// motion skips to the end. A mock
// QTcpServer stands in for the collaboration server, so no Go server is needed.
#include "connectRowParts.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  // Keep the dialog's QSettings reads/writes out of the real per-user config.
  QCoreApplication::setOrganizationName("StencilTest");
  QCoreApplication::setApplicationName("connectRowHeadless");
  // The APP's stylesheet, as main() sets it — without it these metrics are not the app's:
  // `QListWidget::item { padding: 4px }` takes 8px out of every slot, which is the squeeze
  // that pushed the row's buttons off their line.
  app.setStyleSheet(stencil::gui::buildStylesheet(true, "violet"));
  app.setPalette(stencil::gui::buildQPalette(true, "violet"));

  // ── Mock server: any request gets 200 {"token":"tok"} (serves POST /auth/token).
  QTcpServer server;
  check(server.listen(QHostAddress::LocalHost, 0), "mock server listens");
  QObject::connect(&server, &QTcpServer::newConnection, [&] {
    while (QTcpSocket* s = server.nextPendingConnection()) {
      QObject::connect(s, &QTcpSocket::readyRead, [s] {
        s->readAll();
        const QByteArray body = "{\"token\":\"tok\"}";
        s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                 QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        s->flush();
        s->disconnectFromHost();
      });
    }
  });
  const quint16 port = server.serverPort();
  // Userinfo survives normalizeBase (scheme + authority), so this yields a row URL far
  // wider than the dialog — exactly the overflow the elide has to absorb.
  const QString longUrl =
      QStringLiteral("http://a-very-long-user-name-meant-to-stretch-the-connection-row-"
                     "well-past-any-sane-dialog-viewport-width@127.0.0.1:%1")
          .arg(port);
  const QString shortUrl = QStringLiteral("http://u2@127.0.0.1:%1").arg(port);

  ConnectionManager mgr;
  QString err;
  check(stencil::test::connectNow(mgr, longUrl, QString(), err), "connects the long-URL server");

  ConnectDialog dlg(&mgr);
  dlg.resize(480, 420);
  dlg.show();
  pumpFor(50);  // let the show-time layout (and the viewport resize re-cap) settle

  auto* list = dlg.findChild<QListWidget*>();
  check(list != nullptr, "finds the connections list");
  if (!list) return 1;

  QWidget* row = connectrow::checkRowLayout(longUrl, list);
  connectrow::checkRowMotion(server, longUrl, shortUrl, mgr, dlg, list, row);
  connectrow::checkScrollEdges(port, list, row);
  connectrow::checkConnectGestures(server, port, list, row);
  connectrow::checkToastAndHint(server, port, row);

  std::printf("%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
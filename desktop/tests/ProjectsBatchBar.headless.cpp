// Headless checks for the Projects dialog's multi-select surface (dialogs/projectsDialog), split
// across ProjectsBatchBar*.headless.cpp. This TU owns the fixture the sections share: a mock
// QTcpServer standing in for the collaboration server, its connection, and the local project list.
#include "ProjectsBatchBarParts.hpp"

int main(int argc, char** argv) {
  setbuf(stdout, nullptr);  // a crash mid-run must not swallow the progress log
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("StencilTest");
  QCoreApplication::setApplicationName("projectsBatchBarHeadless");

  if (batchDirectionMatrix()) return 1;

  // ── Mock collaboration server: token + a two-project listing; 404 for the rest. ──
  QTcpServer server;
  check(server.listen(QHostAddress::LocalHost, 0), "mock server listens");
  QObject::connect(&server, &QTcpServer::newConnection, [&] {
    while (QTcpSocket* s = server.nextPendingConnection()) {
      auto* buf = new QByteArray;
      QObject::connect(s, &QTcpSocket::readyRead, [s, buf] {
        buf->append(s->readAll());
        if (!buf->contains("\r\n\r\n")) return;  // wait for the full header block
        const QByteArray line = buf->left(buf->indexOf("\r\n"));
        QByteArray body = "{}";
        QByteArray status = "200 OK";
        if (line.startsWith("POST /auth/token")) {
          body = "{\"token\":\"tok\"}";
        } else if (line.startsWith("GET /projects ") || line.startsWith("GET /projects?")) {
          // hasImage matters: sharedProjectsAsync only surfaces image-bearing projects.
          body =
              "{\"projects\":[{\"id\":\"r1\",\"name\":\"delta\",\"hasImage\":true,"
              "\"imageW\":4,\"imageH\":4,\"createdAt\":100,\"version\":1},"
              "{\"id\":\"r2\",\"name\":\"epsilon\",\"hasImage\":true,"
              "\"imageW\":4,\"imageH\":4,\"createdAt\":200,\"version\":1}]}";
        } else {
          status = "404 Not Found";
        }
        s->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: " +
                 QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        buf->clear();
        s->flush();
        s->disconnectFromHost();  // may delete buf synchronously via disconnected
      });
      QObject::connect(s, &QTcpSocket::disconnected, [s, buf] { delete buf; s->deleteLater(); });
    }
  });
  const QString url = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());

  ConnectionManager mgr;
  QString err;
  check(stencil::test::connectNow(mgr, url, QString(), err), "connects to the mock server");

  std::vector<Project> locals;
  locals.push_back(makeLocal("l1", "alpha", 3000));
  locals.push_back(makeLocal("l2", "beta", 2000));
  locals.push_back(makeLocal("l3", "gamma", 1000));

  if (pinnedRows(locals)) return 1;
  if (batchBarVisibility(locals, mgr)) return 1;
  if (batchRemoveAndCheckboxPress(locals)) return 1;
  if (filterTransitions(locals)) return 1;

  std::printf(failures ? "FAILED (%d failures)\n" : "OK\n", failures);
  return failures ? 1 : 0;
}

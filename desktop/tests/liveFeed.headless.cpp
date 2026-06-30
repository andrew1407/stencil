// Headless check for the live push feed (net/liveFeed). Drives a real LiveFeed against a
// mock QTcpServer that speaks the server's NDJSON project-events protocol, asserting it:
//   - sends a well-formed global-feed hello (type=hello, empty projectId, a token),
//   - emits projectUpdated only for "project-event" frames (welcome/garbage ignored),
//   - reassembles a frame split across two TCP writes,
//   - flags a "deleted" event,
//   - declines an https base (the plaintext feed can't ride TLS), and clears on unsubscribe.
// This guards the parse/socket path that the desktop's co-edit latency now depends on,
// without needing a running Go server. Built only when Qt is present (Qt-coupled, like the
// other *.headless tests); not part of the Qt-free core stencil_tests.
#include "liveFeed.hpp"
#include "serverClient.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <cstdio>
#include <functional>
#include <vector>

using namespace stencil::net;

static int failures = 0;
static void check(bool ok, const char* msg) {
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", msg);
  if (!ok) ++failures;
}

// Pump the event loop until pred() holds or a watchdog elapses (keeps the test from hanging).
static void pumpUntil(const std::function<bool()>& pred, int timeoutMs = 3000) {
  QElapsedTimer t;
  t.start();
  while (!pred() && t.elapsed() < timeoutMs) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  QTcpServer server;
  check(server.listen(QHostAddress::LocalHost, 0), "mock server listens");
  const quint16 editPort = server.serverPort();
  // LiveFeed dials (base REST port + 1), so the base's port is the edit port minus one.
  const QString base = QStringLiteral("http://127.0.0.1:%1").arg(editPort - 1);

  QByteArray helloSeen;
  bool pushed = false;
  QTcpSocket* peer = nullptr;
  QObject::connect(&server, &QTcpServer::newConnection, [&] {
    peer = server.nextPendingConnection();
    QObject::connect(peer, &QTcpSocket::readyRead, [&] {
      helloSeen += peer->readAll();
      if (pushed || !helloSeen.contains('\n')) return;  // wait for the hello line
      pushed = true;
      peer->write("{\"type\":\"welcome\",\"version\":1}\n");  // ignored (not a project-event)
      peer->write(
          "{\"type\":\"project-event\",\"event\":\"updated\","
          "\"project\":{\"id\":\"p1\",\"version\":5}}\n");
      // A frame split across two writes must reassemble from the buffer.
      peer->write("{\"type\":\"project-event\",\"event\":\"updated\",\"proj");
      peer->flush();
      peer->write("ect\":{\"id\":\"p1\",\"version\":7}}\n");
      peer->write("not json at all\n");  // ignored (unparseable)
      peer->write(
          "{\"type\":\"project-event\",\"event\":\"deleted\","
          "\"project\":{\"id\":\"p1\",\"version\":8}}\n");
      peer->flush();
    });
  });

  struct Evt {
    QString id;
    qint64 version;
    bool deleted;
  };
  std::vector<Evt> got;
  LiveFeed feed;
  QObject::connect(&feed, &LiveFeed::projectUpdated, [&](const QString& id, qint64 v, bool d) {
    got.push_back({id, v, d});
  });

  // An https origin can't speak the plaintext feed — declined, no subscription left behind.
  check(feed.subscribe("https://example.com:8090", "tok") == false, "https base declined");
  check(feed.base().isEmpty(), "declined https leaves no subscription");

  check(feed.subscribe(base, "tok-123"), "subscribe to an http base");
  check(feed.base() == base, "base() reflects the subscription");
  pumpUntil([&] { return got.size() >= 3; });

  check(got.size() == 3, "three project-events delivered (welcome + garbage ignored)");
  if (got.size() >= 3) {
    check(got[0].id == "p1" && got[0].version == 5 && !got[0].deleted, "updated v5 parsed");
    check(got[1].id == "p1" && got[1].version == 7 && !got[1].deleted, "split frame reassembled (v7)");
    check(got[2].id == "p1" && got[2].version == 8 && got[2].deleted, "deleted event flagged");
  }

  // The hello frame opens the GLOBAL feed: type=hello, empty projectId, a non-empty token.
  {
    const int nl = helloSeen.indexOf('\n');
    check(nl >= 0, "server received a hello line");
    const QJsonObject h = QJsonDocument::fromJson(helloSeen.left(nl)).object();
    check(h.value("type").toString() == QLatin1String("hello"), "hello type");
    check(h.value("projectId").toString().isEmpty(), "hello projectId empty (global feed)");
    check(!h.value("token").toString().isEmpty(), "hello carries the token");
  }

  feed.unsubscribe();
  check(feed.base().isEmpty(), "unsubscribe clears the subscription");

  // ── #1 connection-security policy (ServerClient::normalizeBase / isInsecureRemote) ──
  {
    using SC = ServerClient;
    // Secure by default: a bare REMOTE host gets https; loopback keeps plaintext http.
    check(SC::normalizeBase("example.com:8090") == "https://example.com:8090", "bare remote -> https");
    check(SC::normalizeBase("localhost:8090") == "http://localhost:8090", "bare localhost -> http");
    check(SC::normalizeBase("127.0.0.1:8090") == "http://127.0.0.1:8090", "bare 127.0.0.1 -> http");
    // An explicit scheme is preserved (the user opts into cleartext deliberately).
    check(SC::normalizeBase("http://example.com:8090") == "http://example.com:8090", "explicit http preserved");
    check(SC::normalizeBase("https://example.com:8090/x/") == "https://example.com:8090", "https + path trimmed");
    // isInsecureRemote: only cleartext-to-a-remote-host trips it.
    check(SC::isInsecureRemote("http://example.com:8090"), "http remote flagged insecure");
    check(!SC::isInsecureRemote("http://localhost:8090"), "http loopback not flagged");
    check(!SC::isInsecureRemote("http://127.0.0.1:8090"), "http 127.0.0.1 not flagged");
    check(!SC::isInsecureRemote("https://example.com:8090"), "https remote is secure");
    check(SC::isLoopbackHost("::1"), "ipv6 ::1 is loopback");
  }

  std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}

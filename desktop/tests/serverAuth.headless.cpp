// Headless check of STALE-SESSION handling (net/serverClient + the Servers dialog
// row), mirroring the browser's design:
//   - a credential the server REFUSES (401/403) is its own status — Expired — kept
//     apart from Error, which means "unreachable"; the saved connection survives;
//   - an expired client is never retried in a loop: further calls leave it expired
//     and issue no fresh auth of their own;
//   - an admin token still works (the client mints a session with it);
//   - a valid token still connects normally;
//   - the dialog row for an expired connection renders the amber note and a
//     labelled Reconnect, and signing in again turns it green.
// A mock QTcpServer stands in for the collaboration server, so no Go server is
// needed (same approach as connectRow.headless).
#include "connectDialog.hpp"
#include "serverClient.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <cstdio>
#include <functional>

using stencil::gui::ConnectDialog;
using stencil::net::ConnectionManager;
using stencil::net::ServerClient;

#include "support/check.hpp"
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
          const int status = mint ? tokenStatus
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

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("StencilTest");
  QCoreApplication::setApplicationName("serverAuthHeadless");

  MockServer mock;
  check(mock.listen(), "mock server listens");

  // ── a REFUSED credential is Expired, not Error ──
  std::printf("classification:\n");
  {
    mock.projectsStatus = 401;   // the token is not a session token…
    mock.tokenStatus = 401;      // …and minting with it is refused too
    ConnectionManager mgr;
    QString err;
    check(!mgr.connectTo(mock.url(), QStringLiteral("stale-token"), err),
          "a refused token does not connect");
    ServerClient* cl = mgr.find(mock.url());
    check(cl != nullptr, "the connection is KEPT after a refusal");
    if (cl) {
      check(cl->status() == ServerClient::Status::Expired,
            "a refused credential is Expired, not Error");
      check(cl->needsReauth(), "…and says it needs re-authentication");
      check(cl->base() == ServerClient::normalizeBase(mock.url()), "the URL survives");

      // ── no retry loop: further work leaves it expired and mints nothing ──
      const int before = mock.tokenRequests;
      cl->listProjectsAsync([](bool, QVector<stencil::net::ServerProject>) {});
      pumpFor(150);
      check(cl->status() == ServerClient::Status::Expired, "it stays expired");
      check(mock.tokenRequests == before,
            "an expired client does not re-auth behind the user's back");
    }
  }

  // ── an UNREACHABLE server is Error, and NOT flagged for re-auth ──
  {
    ConnectionManager mgr;
    QString err;
    // A port nothing listens on: the connection fails at the transport.
    const QString dead = QStringLiteral("http://127.0.0.1:%1").arg(mock.server.serverPort() + 7);
    mgr.connectTo(dead, QString(), err);
    ServerClient* cl = mgr.find(dead);
    if (cl) {
      check(cl->status() != ServerClient::Status::Expired,
            "an unreachable host is not an expired session");
      check(!cl->needsReauth(), "…and offers no re-auth");
    } else {
      check(true, "an unreachable host is not an expired session");
      check(true, "…and offers no re-auth");
    }
  }

  // ── a live session going stale mid-flight ──
  {
    mock.projectsStatus = 200;
    mock.tokenStatus = 200;
    ConnectionManager mgr;
    QString err;
    check(mgr.connectTo(mock.url(), QString(), err), "a fresh session connects");
    ServerClient* cl = mgr.find(mock.url());
    check(cl && cl->status() == ServerClient::Status::Connected, "…and reads as connected");
    mock.projectsStatus = 401;   // the session lapses on the server
    const int mintsBefore = mock.tokenRequests;
    bool done = false;
    cl->listProjectsAsync([&](bool, QVector<stencil::net::ServerProject>) { done = true; });
    pumpUntil([&] { return done; });
    check(cl->status() == ServerClient::Status::Expired,
          "a live session refused mid-flight becomes Expired");
    check(mock.tokenRequests == mintsBefore,
          "…with no re-mint: an anonymous session has no credential to mint with");
  }

  // ── the ADMIN token path still mints a session ──
  {
    mock.projectsStatus = 401;   // not a session token…
    mock.tokenStatus = 200;      // …but minting with it works: it is the admin token
    ConnectionManager mgr;
    QString err;
    check(mgr.connectTo(mock.url(), QStringLiteral("admin-token"), err),
          "an admin token connects by minting a session");
    ServerClient* cl = mgr.find(mock.url());
    check(cl && cl->status() == ServerClient::Status::Connected, "…and lands Connected");
  }

  // ── a valid token connects normally ──
  {
    mock.projectsStatus = 200;
    mock.tokenStatus = 200;
    ConnectionManager mgr;
    QString err;
    check(mgr.connectTo(mock.url(), QStringLiteral("good-token"), err),
          "a valid session token connects");
    ServerClient* cl = mgr.find(mock.url());
    check(cl && cl->status() == ServerClient::Status::Connected && !cl->needsReauth(),
          "…with no re-auth needed");
  }

  // ── mid-session re-mint: the stored credential rescues a lapsed session ──
  std::printf("mid-session re-mint:\n");
  {
    mock.projectsStatus = 200;
    mock.tokenStatus = 200;
    mock.goodBearer = "sess1";
    mock.mintToken = "sess1";
    ConnectionManager mgr;
    QString err;
    // Pasting the admin token: it can't list projects, but it mints "sess1".
    check(mgr.connectTo(mock.url(), QStringLiteral("admin-token"), err),
          "the admin credential connects by minting");
    ServerClient* cl = mgr.find(mock.url());
    check(cl && cl->token() == QStringLiteral("sess1"), "…and holds the minted session");

    // The server restarts: it forgets "sess1" and mints "sess2" now.
    mock.goodBearer = "sess2";
    mock.mintToken = "sess2";
    const int mintsBefore = mock.tokenRequests;
    bool called = false, ok = false;
    cl->listProjectsAsync([&](bool o, QVector<stencil::net::ServerProject>) { ok = o; called = true; });
    pumpUntil([&] { return called; });
    check(ok, "a lapsed session re-mints with the credential and retries in place");
    check(cl->token() == QStringLiteral("sess2"), "…adopting the fresh session token");
    check(cl->credential() == QStringLiteral("admin-token"), "…never replacing the credential");
    check(cl->status() == ServerClient::Status::Connected, "…without ever reading as expired");
    check(mock.tokenRequests == mintsBefore + 1, "exactly one mint for the rescue");
  }

  // ── the re-mint happens ONCE only: a dead credential lands Expired ──
  {
    mock.goodBearer = "sess3";
    mock.mintToken = "sess3";
    mock.tokenStatus = 200;
    ConnectionManager mgr;
    QString err;
    check(mgr.connectTo(mock.url(), QStringLiteral("admin-token"), err), "reconnects for the dead-credential round");
    ServerClient* cl = mgr.find(mock.url());
    // The server restarts AND rotates its admin token: nothing this client holds works.
    mock.goodBearer = "sess4";
    mock.tokenStatus = 401;
    const int mintsBefore = mock.tokenRequests;
    bool called = false, ok = true;
    cl->listProjectsAsync([&](bool o, QVector<stencil::net::ServerProject>) { ok = o; called = true; });
    pumpUntil([&] { return called; });
    check(!ok, "the rescue fails when the credential no longer mints");
    check(cl->status() == ServerClient::Status::Expired, "…and lands Expired");
    check(mock.tokenRequests == mintsBefore + 1, "…after exactly ONE mint attempt (no loop)");
    mock.goodBearer.clear();  // back to the scripted per-path statuses
    mock.mintToken = "tok";
  }

  // ── the expired ROW: amber note + a labelled Reconnect that signs in again ──
  std::printf("expired row:\n");
  {
    mock.projectsStatus = 401;
    mock.tokenStatus = 401;
    ConnectionManager mgr;
    QString err;
    mgr.connectTo(mock.url(), QStringLiteral("stale-token"), err);
    ServerClient* cl = mgr.find(mock.url());
    check(cl && cl->status() == ServerClient::Status::Expired, "the row's client is expired");

    ConnectDialog dlg(&mgr);
    dlg.resize(520, 420);
    dlg.show();
    pumpFor(60);
    auto* note = dlg.findChild<QLabel*>(QStringLiteral("expiredNote"));
    check(note != nullptr, "the expired row carries its own note");
    if (note)
      check(note->text().contains("Session expired") &&
                note->toolTip().contains("reconnect to sign in again"),
            "…saying the session expired, with the full sentence on its tooltip");
    auto* signIn = dlg.findChild<QPushButton*>(QStringLiteral("expiredReconnect"));
    check(signIn != nullptr, "…and a LABELLED Reconnect action");
    if (signIn) check(signIn->text().contains("Reconnect"), "…that says Reconnect");

    // The server starts handing out sessions again: the row's own reconnect
    // (fresh-session path, no prompt) signs in and the row stops being expired.
    mock.projectsStatus = 200;
    mock.tokenStatus = 200;
    if (signIn) signIn->click();
    pumpUntil([&] {
      ServerClient* c = mgr.find(mock.url());
      return c && c->status() == ServerClient::Status::Connected;
    });
    ServerClient* after = mgr.find(mock.url());
    check(after && after->status() == ServerClient::Status::Connected,
          "reconnect signs in again without a prompt");
    pumpFor(60);
    check(dlg.findChild<QLabel*>(QStringLiteral("expiredNote")) == nullptr,
          "…and the expired note leaves with it");
  }

  std::printf(failures ? "FAILURE (%d failures)\n" : "OK\n", failures);
  return failures ? 1 : 0;
}

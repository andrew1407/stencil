// Headless check of STALE-SESSION handling (net/serverClient + the Servers dialog
// row), mirroring the browser's design:
//   - a credential the server REFUSES (401/403) is its own status — Expired — kept
//     apart from Error, which means "unreachable"; the saved connection survives;
//   - an expired client is never retried in a loop: further calls leave it expired
//     and issue no fresh auth of their own;
//   - an admin token still works (the client mints a session with it);
//   - a valid token still connects normally;
//   - the dialog row for an expired connection wears the amber card and a LABELLED
//     amber Reconnect (browser .connect-expired: the card says what is wrong, so there
//     is no separate note), and signing in again turns it green.
// …plus the credential KIND that rides along with it (browser credentialKind parity):
// Admin when the credential proved it can mint a session, Session when the token passed
// the /projects probe, None otherwise — persisted with the saved connections and shown as
// the row's golden band, its Invite button, and the All / Admin / Non-admin filter, whose
// changes play as a question re-answered (support/filterFade).
// A mock QTcpServer stands in for the collaboration server, so no Go server is needed.
#include "connectDialog.hpp"
#include "connectionStore.hpp"
#include "disintegrateOverlay.hpp"  // the DESTRUCTIVE effect a filter-out must not use
#include "filterFade.hpp"           // the light filter transition it uses instead
#include "serverClient.hpp"

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

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("StencilTest");
  QCoreApplication::setApplicationName("serverAuthHeadless");

  MockServer mock;
  check(mock.listen(), "mock server listens");
  MockServer mock2;   // a SECOND origin, so one manager can hold two kinds of row
  check(mock2.listen(), "second mock server listens");

  // ── a REFUSED credential is Expired, not Error ──
  std::printf("classification:\n");
  {
    mock.projectsStatus = 401;   // the token is not a session token…
    mock.tokenStatus = 401;      // …and minting with it is refused too
    ConnectionManager mgr;
    QString err;
    check(!stencil::test::connectNow(mgr, mock.url(), QStringLiteral("stale-token"), err),
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
    stencil::test::connectNow(mgr, dead, QString(), err);
    ServerClient* cl = mgr.find(dead);
    if (cl) {
      check(cl->status() != ServerClient::Status::Expired,
            "an unreachable host is not an expired session");
      check(!cl->needsReauth(), "…and offers no re-auth");
    } else {
      check(true, "an unreachable host is not an expired session");
      check(true, "…and offers no re-auth");
    }
    // …and the toast reads like the browser's. A request that never reached the server
    // has no status, so the browser's fetch rejection carries the TRANSPORT's message —
    // "Could not connect — token request failed (HTTP 0)" was the desktop's own invention.
    check(!err.contains(QLatin1String("HTTP 0")), "a dead host never reports \"HTTP 0\"");
    check(!err.isEmpty() && !err.contains(QLatin1String("HTTP")),
          "…it reports what the transport said instead");
  }

  // ── a REST failure reads exactly as the browser words it ──────────────────
  // browser connectionManager.js _req: "<METHOD> <path>: HTTP <status>" (or the server's
  // own JSON `message`), which the connect toast then wraps in "Could not connect — …".
  {
    ConnectionManager mgr;
    QString err;
    mock.tokenStatus = 500;
    stencil::test::connectNow(mgr, mock.url(), QString(), err);
    mock.tokenStatus = 200;
    check(err == QLatin1String("POST /auth/token: HTTP 500"),
          "a REST failure is worded exactly as the browser words it");
  }

  // ── a live session going stale mid-flight ──
  {
    mock.projectsStatus = 200;
    mock.tokenStatus = 200;
    ConnectionManager mgr;
    QString err;
    check(stencil::test::connectNow(mgr, mock.url(), QString(), err), "a fresh session connects");
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
    check(stencil::test::connectNow(mgr, mock.url(), QStringLiteral("admin-token"), err),
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
    check(stencil::test::connectNow(mgr, mock.url(), QStringLiteral("good-token"), err),
          "a valid session token connects");
    ServerClient* cl = mgr.find(mock.url());
    check(cl && cl->status() == ServerClient::Status::Connected && !cl->needsReauth(),
          "…with no re-auth needed");
  }

  // ── the credential KIND: admin / non-admin / none ──
  std::printf("credential kind:\n");
  {
    // ADMIN: the token cannot list projects, but it MINTS a session.
    mock.projectsStatus = 200;
    mock.tokenStatus = 200;
    mock.goodBearer = "sess-k";
    mock.mintToken = "sess-k";
    mock.mintBearer = "admin-token";   // only the credential may mint
    ConnectionManager mgr;
    QString err;
    check(stencil::test::connectNow(mgr, mock.url(), QStringLiteral("admin-token"), err),
          "an admin credential connects");
    ServerClient* cl = mgr.find(mock.url());
    check(cl && cl->credentialKind() == ServerClient::CredentialKind::Admin,
          "a credential that MINTED the session is Admin");
    check(cl && cl->isAdmin(), "…and reads as admin");
    mock.mintBearer.clear();
  }
  {
    // NON-ADMIN: the supplied token passed the /projects probe on its own.
    mock.goodBearer = "good-token";
    ConnectionManager mgr;
    QString err;
    check(stencil::test::connectNow(mgr, mock.url(), QStringLiteral("good-token"), err),
          "a session token connects");
    ServerClient* cl = mgr.find(mock.url());
    check(cl && cl->credentialKind() == ServerClient::CredentialKind::Session,
          "a token that PASSED the probe is Session, never Admin");
    check(cl && !cl->isAdmin(), "…so it claims no admin powers");
    mock.goodBearer.clear();
  }
  {
    // NONE: nothing was supplied — the session was minted anonymously.
    ConnectionManager mgr;
    QString err;
    check(stencil::test::connectNow(mgr, mock.url(), QString(), err), "an anonymous session connects");
    ServerClient* cl = mgr.find(mock.url());
    check(cl && cl->credentialKind() == ServerClient::CredentialKind::None,
          "an anonymous session holds no credential kind at all");
    check(cl && !cl->isAdmin(), "…and is not admin");
  }
  {
    // A SESSION credential the server later forgets: the mid-session re-mint proves
    // it can mint after all, so the kind is promoted (browser _req parity).
    mock.goodBearer = "sess-p";
    mock.mintToken = "sess-p";
    ConnectionManager mgr;
    QString err;
    check(stencil::test::connectNow(mgr, mock.url(), QStringLiteral("sess-p"), err), "a session token connects");
    ServerClient* cl = mgr.find(mock.url());
    check(cl && cl->credentialKind() == ServerClient::CredentialKind::Session,
          "…classified Session at connect");
    mock.goodBearer = "sess-q";   // the server restarts: it mints "sess-q" now
    mock.mintToken = "sess-q";
    bool called = false, ok = false;
    cl->listProjectsAsync([&](bool o, QVector<stencil::net::ServerProject>) { ok = o; called = true; });
    pumpUntil([&] { return called; });
    check(ok, "the lapsed session is rescued by the credential");
    check(cl->credentialKind() == ServerClient::CredentialKind::Admin,
          "…and a credential that minted mid-session is now known Admin");
    mock.goodBearer.clear();
    mock.mintToken = "tok";
  }
  {
    // A kind already PROVEN skips the doomed probe: one request, straight to the mint.
    mock.goodBearer = "sess-h";
    mock.mintToken = "sess-h";
    mock.mintBearer = "admin-token";
    ConnectionManager mgr;
    QString err;
    const int before = mock.requests;
    check(stencil::test::connectNow(mgr, mock.url(), QStringLiteral("admin-token"), err,
                        ServerClient::CredentialKind::Admin),
          "a RESTORED admin credential connects");
    ServerClient* cl = mgr.find(mock.url());
    check(cl && cl->isAdmin() && cl->token() == QStringLiteral("sess-h"),
          "…minting its session at once");
    check(mock.requests == before + 1,
          "…in ONE request: a known admin credential never probes /projects");
    mock.mintBearer.clear();
    mock.goodBearer.clear();
    mock.mintToken = "tok";
  }

  // ── the kind PERSISTS with the saved connections (compatibly) ──
  std::printf("kind persistence:\n");
  {
    mock.goodBearer = "sess-s";
    mock.mintToken = "sess-s";
    mock.mintBearer = "admin-token";
    mock2.goodBearer = "plain-tok";
    ConnectionManager mgr;
    QString err;
    check(stencil::test::connectNow(mgr, mock.url(), QStringLiteral("admin-token"), err), "an admin row");
    check(stencil::test::connectNow(mgr, mock2.url(), QStringLiteral("plain-tok"), err), "…and a non-admin one");
    const auto snap = mgr.snapshot();
    check(snap.size() == 2, "both connections are in the snapshot");
    stencil::net::connectionStore::saveServers(snap);
    const auto back = stencil::net::connectionStore::loadSavedServers();
    check(back.size() == 2, "…and both come back");
    if (back.size() == 2) {
      check(back[0].token == QStringLiteral("admin-token") &&
                back[0].kind == QStringLiteral("admin"),
            "the admin credential round-trips with its kind");
      check(back[1].token == QStringLiteral("plain-tok") &&
                back[1].kind == QStringLiteral("session"),
            "…and the session one with its own");
      check(ServerClient::kindFromTag(back[0].kind) == ServerClient::CredentialKind::Admin &&
                ServerClient::kindFromTag(back[1].kind) == ServerClient::CredentialKind::Session,
            "…both parsing back into the kind they were saved from");
    }
    mock.mintBearer.clear();
    mock.goodBearer.clear();
    mock2.goodBearer.clear();
    mock.mintToken = "tok";
  }
  {
    // OLD rows (written before the kind existed) still load: "url\ttoken", kind unknown.
    QSettings s;
    s.setValue(QStringLiteral("connections/servers"),
               QStringList{QStringLiteral("http://old.example:8090\told-tok")});
    const auto legacy = stencil::net::connectionStore::loadSavedServers();
    check(legacy.size() == 1 && legacy[0].url == QStringLiteral("http://old.example:8090") &&
              legacy[0].token == QStringLiteral("old-tok") && legacy[0].kind.isEmpty(),
          "a pre-kind row loads unchanged, with no kind");
    check(!legacy.isEmpty() &&
              ServerClient::kindFromTag(legacy[0].kind) == ServerClient::CredentialKind::None,
          "…which reads as no kind (it probes, like it always did)");
    // …and only a RECOGNISED trailing tag is a kind, so a token with a tab survives.
    s.setValue(QStringLiteral("connections/servers"),
               QStringList{QStringLiteral("http://old.example:8090\tto\tken")});
    const auto tabbed = stencil::net::connectionStore::loadSavedServers();
    check(tabbed.size() == 1 && tabbed[0].token == QStringLiteral("to\tken") &&
              tabbed[0].kind.isEmpty(),
          "…and a trailing field that is not a kind tag stays part of the token");
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
    check(stencil::test::connectNow(mgr, mock.url(), QStringLiteral("admin-token"), err),
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
    check(stencil::test::connectNow(mgr, mock.url(), QStringLiteral("admin-token"), err), "reconnects for the dead-credential round");
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

  // ── the expired ROW: amber card + the row's own Reconnect, which signs in again ──
  std::printf("expired row:\n");
  {
    mock.projectsStatus = 401;
    mock.tokenStatus = 401;
    ConnectionManager mgr;
    QString err;
    stencil::test::connectNow(mgr, mock.url(), QStringLiteral("stale-token"), err);
    ServerClient* cl = mgr.find(mock.url());
    check(cl && cl->status() == ServerClient::Status::Expired, "the row's client is expired");

    ConnectDialog dlg(&mgr);
    dlg.resize(520, 420);
    dlg.show();
    pumpFor(60);
    // No separate note any more: the browser says it with the amber card and the amber
    // dot, and the desktop says it the same way. The sentence lives on the dot's and
    // the URL's tooltips.
    check(dlg.findChild<QLabel*>(QStringLiteral("expiredNote")) == nullptr,
          "the expired row carries no note of its own (the browser has none)");
    bool saysExpired = false;
    for (QLabel* l : dlg.findChildren<QLabel*>())
      if (l->toolTip().contains("reconnect to sign in again")) saysExpired = true;
    check(saysExpired, "…the full sentence is on the row's own tooltips");
    // …and the row itself wears the amber card (browser .connect-expired).
    check(dlg.findChildren<QWidget*>(QStringLiteral("connRowExpired")).size() == 1,
          "…and the row wears the expired amber state");
    auto* signIn = dlg.findChild<QPushButton*>(QStringLiteral("expiredReconnect"));
    check(signIn != nullptr, "…and a Reconnect action");
    if (signIn) {
      // ICON-ONLY like every other row action (browser: .connect-reconnect-one keeps its
      // .btn-icon shape here too) — the amber fill and the tooltip carry the state.
      check(signIn->text().isEmpty() && !signIn->icon().isNull(),
            "…that is the plain icon button, no label");
      check(signIn->toolTip().contains("Sign in to this server again"),
            "…with what it does on its tooltip");
      // Exactly the disconnect button's box — the two sit side by side, same square.
      auto* disc = dlg.findChild<QPushButton*>(QStringLiteral("rowDisconnect"));
      check(signIn->height() == disc->height(),
            "…and the same height as the row's disconnect button");
      check(signIn->width() == disc->width(), "…and the same width");
    }

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

  // ── A pasted token signs the EXPIRED row in ──────────────────────────────────
  // The refused client keeps its place in the list on purpose, so the sign-in cannot go
  // through connectToAsync() — that answers "already connected" and leaves the session
  // expired however good the token is. reauthenticateAsync() reuses the listed client.
  std::printf("expired row: signing in with a pasted token:\n");
  {
    mock.projectsStatus = 401;
    mock.tokenStatus = 401;
    ConnectionManager m2;
    QString err0;
    stencil::test::connectNow(m2, mock.url(), QStringLiteral("stale-token"), err0);
    ServerClient* c = m2.find(mock.url());
    check(c && c->status() == ServerClient::Status::Expired, "an expired row to sign in");

    QString cerr;
    check(!stencil::test::connectNow(m2, mock.url(), QStringLiteral("admin-token"), cerr),
          "connectToAsync refuses a url already in the list…");
    check(cerr == QStringLiteral("already connected"), "…saying exactly that");

    mock.projectsStatus = 200;
    mock.tokenStatus = 200;
    QString rerr;
    check(stencil::test::reauthNow(m2, mock.url(), QStringLiteral("admin-token"), rerr),
          "…but reauthenticate signs the same row in with the pasted token");
    ServerClient* back = m2.find(mock.url());
    check(back && back->status() == ServerClient::Status::Connected,
          "…and the connection is live again");
    check(m2.urls().size() == 1, "…in the one row it always was");
  }

  // ── invite links: "<url>#token=<tok>" feeds the credential flow ──
  std::printf("invite links:\n");
  {
    // The pure helpers first.
    QString tok;
    check(ServerClient::splitInviteToken("http://h:1#token=abc", tok) == "http://h:1" &&
              tok == "abc",
          "splitInviteToken strips the fragment and yields the token");
    check(ServerClient::splitInviteToken("http://h:1", tok) == "http://h:1" && tok.isEmpty(),
          "…leaves a fragmentless URL alone");
    check(ServerClient::splitInviteToken("http://h:1#other", tok) == "http://h:1#other" &&
              tok.isEmpty(),
          "…and ignores a non-token fragment");
    check(ServerClient::inviteLink("http://h:1", "abc") == "http://h:1#token=abc",
          "inviteLink builds <url>#token=<tok>");

    // Connecting THROUGH an invite link: the fragment token is the credential.
    mock.tokenStatus = 200;
    mock.projectsStatus = 200;
    mock.goodBearer = "invite-tok";
    ConnectionManager mgr;
    QString err;
    check(stencil::test::connectNow(mgr, mock.url() + "#token=invite-tok", QString(), err),
          "an invite link connects");
    ServerClient* cl = mgr.find(mock.url());
    check(cl != nullptr, "…registered at the fragmentless base");
    if (cl) {
      check(cl->base() == ServerClient::normalizeBase(mock.url()),
            "…with the fragment stripped from the base URL");
      check(cl->credential() == QStringLiteral("invite-tok"),
            "…and the fragment token as the credential");
      check(cl->status() == ServerClient::Status::Connected, "…landing Connected");
    }
    mock.goodBearer.clear();
  }

  // ── an explicitly-typed token wins over the link's fragment ──
  {
    mock.goodBearer = "typed-tok";
    mock.tokenStatus = 401;  // minting refused: only the typed token can get in
    ConnectionManager mgr;
    QString err;
    check(stencil::test::connectNow(mgr, mock.url() + "#token=stale-frag", QStringLiteral("typed-tok"), err),
          "a typed token connects even alongside a stale fragment");
    ServerClient* cl = mgr.find(mock.url());
    check(cl && cl->credential() == QStringLiteral("typed-tok"),
          "…and the typed token IS the credential (fragment ignored)");
    mock.goodBearer.clear();
    mock.tokenStatus = 200;
  }

  // ── invite mint: a fresh token via the credential, without touching the session ──
  std::printf("invite mint:\n");
  {
    mock.goodBearer = "sess-a";
    mock.mintToken = "sess-a";
    mock.mintBearer = "admin-token";  // the mint route now demands the credential
    ConnectionManager mgr;
    QString err;
    check(stencil::test::connectNow(mgr, mock.url(), QStringLiteral("admin-token"), err),
          "connects for the invite round");
    ServerClient* cl = mgr.find(mock.url());
    check(cl && cl->token() == QStringLiteral("sess-a"), "…holding its own session");

    mock.mintToken = "fresh-tok";  // what the invite mint hands out
    bool called = false, ok = false;
    QString link;
    cl->mintInviteAsync([&](bool o, QString l) { ok = o; link = l; called = true; });
    pumpUntil([&] { return called; });
    check(ok, "the invite mint succeeds with the CREDENTIAL as bearer");
    check(link == cl->base() + "#token=fresh-tok", "…yielding <url>#token=<fresh>");
    check(cl->token() == QStringLiteral("sess-a"), "…without touching the live session token");
    check(cl->credential() == QStringLiteral("admin-token"), "…or the credential");

    // Round-trip: the minted link signs a second client in as its own session.
    mock.goodBearer = "fresh-tok";
    ConnectionManager mgr2;
    check(stencil::test::connectNow(mgr2, link, QString(), err), "the minted link connects a fresh client");
    ServerClient* cl2 = mgr2.find(mock.url());
    check(cl2 && cl2->status() == ServerClient::Status::Connected &&
              cl2->credential() == QStringLiteral("fresh-tok"),
          "…which holds the invited token as its credential");

    // An anonymous session has no credential — the mint refuses locally.
    mock.goodBearer.clear();
    mock.mintBearer.clear();
    ConnectionManager mgr3;
    check(stencil::test::connectNow(mgr3, mock.url(), QString(), err), "an anonymous session for contrast");
    ServerClient* cl3 = mgr3.find(mock.url());
    const int mintsBefore = mock.tokenRequests;
    bool called3 = false, ok3 = true;
    if (cl3) cl3->mintInviteAsync([&](bool o, QString) { ok3 = o; called3 = true; });
    pumpFor(100);
    check(called3 && !ok3, "an anonymous session refuses to mint an invite");
    check(mock.tokenRequests == mintsBefore, "…without asking the server");
    mock.mintToken = "tok";
  }

  // ── the invite BUTTON: credentialed rows only; a click copies the link ──
  std::printf("invite row:\n");
  {
    mock.goodBearer = "sess-b";
    mock.mintToken = "sess-b";
    mock.mintBearer = "admin-token";
    ConnectionManager mgr;
    QString err;
    check(stencil::test::connectNow(mgr, mock.url(), QStringLiteral("admin-token"), err),
          "connects for the invite-row round");
    ConnectDialog dlg(&mgr);
    dlg.resize(520, 420);
    dlg.show();
    pumpFor(60);
    auto* invite = dlg.findChild<QPushButton*>(QStringLiteral("inviteBtn"));
    check(invite != nullptr, "a connected row with a credential offers Invite");
    mock.mintToken = "invited-tok";
    if (invite) invite->click();
    const QString want =
        ServerClient::normalizeBase(mock.url()) + "#token=invited-tok";
    pumpUntil([&] { return QGuiApplication::clipboard()->text() == want; });
    check(QGuiApplication::clipboard()->text() == want,
          "clicking Invite puts <url>#token=<fresh> on the clipboard");

    // An anonymous connection shows no Invite — nothing to mint with.
    mock.goodBearer.clear();
    mock.mintBearer.clear();
    ConnectionManager mgr2;
    check(stencil::test::connectNow(mgr2, mock.url(), QString(), err), "an anonymous connection for contrast");
    ConnectDialog dlg2(&mgr2);
    dlg2.resize(520, 420);
    dlg2.show();
    pumpFor(60);
    check(dlg2.findChild<QPushButton*>(QStringLiteral("inviteBtn")) == nullptr,
          "…whose row offers no Invite");
    mock.mintToken = "tok";
  }

  // ── the ADMIN row: golden band + tooltip, Invite gated on the kind, kind filter ──
  std::printf("admin row:\n");
  {
    mock.goodBearer = "sess-r";
    mock.mintToken = "sess-r";
    mock.mintBearer = "admin-token";
    mock2.goodBearer = "plain-tok";
    ConnectionManager mgr;
    QString err;
    check(stencil::test::connectNow(mgr, mock.url(), QStringLiteral("admin-token"), err),
          "an admin connection for the row round");
    check(stencil::test::connectNow(mgr, mock2.url(), QStringLiteral("plain-tok"), err),
          "…and a non-admin one beside it");
    ConnectDialog dlg(&mgr);
    dlg.resize(560, 460);
    dlg.show();
    pumpFor(60);

    const auto gold = dlg.findChildren<QWidget*>(QStringLiteral("connRowAdmin"));
    check(gold.size() == 1, "exactly the ADMIN row wears the golden band");
    auto* rowList = dlg.findChild<QListWidget*>(QStringLiteral("connList"));
    if (!gold.isEmpty() && rowList) {
      // The card styles live on the LIST (one cascading sheet); the row carries the
      // objectName its rule selects.
      check(rowList->styleSheet().contains(
                QStringLiteral("QWidget#connRowAdmin{border:2px solid #d4a017")),
            "…drawn in the app's collaboration gold");
      check(gold[0]->toolTip().contains(QStringLiteral("mint session tokens")),
            "…and saying on its tooltip what the credential can do");
      // The lock + "Admin" pair beside the URL, at its natural width — a QLabel host
      // measured itself from its own empty text and squeezed the pair to a sliver.
      auto* badge = dlg.findChild<QWidget*>(QStringLiteral("connAdminBadge"));
      check(badge != nullptr, "…and carrying the Admin badge");
      if (badge) {
        int parts = 0;
        for (QLabel* l : badge->findChildren<QLabel*>()) parts += l->sizeHint().width();
        check(parts > 0 && badge->sizeHint().width() >= parts,
              "…wide enough for the lock glyph and the word, not a sliver");
      }
      // Browser .connect-row parity: the plain rows are filled cards too — radius 8,
      // hovering to --bg-info.
      check(rowList->styleSheet().contains(QStringLiteral("border-radius:8px")) &&
                rowList->styleSheet().contains(QStringLiteral("QWidget#connRow[hovered=\"true\"]")),
            "…and every row is a rounded card with a hover wash");
    }
    // The Invite button follows the KIND now, not merely "has a credential": a session
    // token cannot mint (the server 401s it), so only the admin row offers one.
    check(dlg.findChildren<QPushButton*>(QStringLiteral("inviteBtn")).size() == 1,
          "only the admin row offers Invite");

    auto* filter = dlg.findChild<QComboBox*>(QStringLiteral("connKindFilter"));
    auto* lw = dlg.findChild<QListWidget*>(QStringLiteral("connList"));
    check(filter != nullptr, "the list carries an All / Admin / Non-admin filter");
    check(lw != nullptr, "…over the connections list");
    const QStringList urls = mgr.urls();
    const int adminRow = urls.indexOf(ServerClient::normalizeBase(mock.url()));
    const int plainRow = urls.indexOf(ServerClient::normalizeBase(mock2.url()));
    check(adminRow >= 0 && plainRow >= 0, "both rows are in the list");
    if (filter && lw && adminRow >= 0 && plainRow >= 0) {
      check(filter->count() == 3 && filter->currentData().toString() == QLatin1String("all"),
            "…three ways, All by default");
      check(!lw->item(adminRow)->isHidden() && !lw->item(plainRow)->isHidden(),
            "…which shows every row");
      const int fullH = lw->item(plainRow)->data(FILTER_FULL_HEIGHT_ROLE).toInt();
      check(fullH > 0 && lw->item(plainRow)->sizeHint().height() == fullH,
            "a settled row occupies its whole slot");

      // ── A filter is a QUESTION re-answered, not a removal: the row it excludes was
      // never disconnected, so there is no exit to watch — it is out of the view the
      // moment the answer changes, and the surviving row keeps the slot it had.
      filter->setCurrentIndex(filter->findData(QStringLiteral("admin")));
      check(lw->item(plainRow)->isHidden() && lw->item(plainRow)->sizeHint().height() == 0,
            "an excluded row is gone at once, slot closed");
      check(!filteredIn(lw->item(plainRow)), "…and has left the filtered set");
      check(filteredIn(lw->item(adminRow)), "…while the matching row is still in it");
      check(!lw->item(adminRow)->isHidden() && lw->item(adminRow)->sizeHint().height() == fullH,
            "Admin leaves the surviving row where it was, at full height");
      // The semantics: a filter-out is NOT a removal, so it never spends the dust.
      check(dlg.findChild<QWidget*>(DisintegrateOverlay::OBJECT_NAME) == nullptr,
            "…with none of the destructive scatter a disconnect uses");

      // ── …and a row the filter REVEALS opens its slot: up at once, still growing.
      filter->setCurrentIndex(filter->findData(QStringLiteral("nonadmin")));
      check(!lw->item(plainRow)->isHidden() && lw->item(plainRow)->sizeHint().height() < fullH,
            "a revealed row is in the view at once, still expanding");
      check(filteredIn(lw->item(plainRow)), "…and already counted in the filtered set");
      check(lw->item(adminRow)->isHidden(), "Non-admin hides the admin row, at once");
      pumpUntil([&] { return lw->item(plainRow)->sizeHint().height() == fullH; });
      check(lw->item(plainRow)->sizeHint().height() == fullH,
            "…with the arrived row landing on its full slot");

      // ── Rapid changes: whatever is mid-flight, the LAST pick decides the visible set.
      for (const char* mode : {"admin", "nonadmin", "admin", "all"}) {
        filter->setCurrentIndex(filter->findData(QString::fromLatin1(mode)));
        pumpFor(FILTER_FADE_MS / 6);   // each flip interrupts the one before it
      }
      pumpUntil([&] {
        return !lw->item(adminRow)->isHidden() && !lw->item(plainRow)->isHidden() &&
               lw->item(adminRow)->sizeHint().height() == fullH &&
               lw->item(plainRow)->sizeHint().height() == fullH;
      });
      check(!lw->item(adminRow)->isHidden() && !lw->item(plainRow)->isHidden(),
            "…and All brings both back");
      check(lw->item(adminRow)->sizeHint().height() == fullH &&
                lw->item(plainRow)->sizeHint().height() == fullH,
            "no row is left stuck part-collapsed after a rapid sequence");
      for (int i = 0; i < lw->count(); ++i)
        check(filteredIn(lw->item(i)) == !lw->item(i)->isHidden(),
              "…and every row's hidden state agrees with the filtered set");
    }

    // Nothing matching says so, instead of leaving an empty list unexplained.
    ConnectionManager lone;
    check(stencil::test::connectNow(lone, mock2.url(), QStringLiteral("plain-tok"), err),
          "a lone non-admin connection");
    ConnectDialog dlg2(&lone);
    dlg2.resize(560, 460);
    dlg2.show();
    pumpFor(60);
    auto* f2 = dlg2.findChild<QComboBox*>(QStringLiteral("connKindFilter"));
    auto* l2 = dlg2.findChild<QListWidget*>(QStringLiteral("connList"));
    if (f2 && l2) {
      f2->setCurrentIndex(f2->findData(QStringLiteral("admin")));
      QListWidgetItem* last = l2->count() ? l2->item(l2->count() - 1) : nullptr;
      check(l2->count() == 2 && last && !last->isHidden() &&
                last->text().contains(QStringLiteral("admin credential")),
            "a filter that matches nothing shows an explanatory line");
      f2->setCurrentIndex(f2->findData(QStringLiteral("all")));
      check(l2->count() == 1 && !l2->item(0)->isHidden(),
            "…which leaves again once rows match");
      pumpUntil([&] { return !l2->item(0)->isHidden(); });

      // ── Reduced motion: the same result, reached with no transition at all.
      qputenv("STENCIL_NO_ANIM", "1");
      f2->setCurrentIndex(f2->findData(QStringLiteral("admin")));
      check(l2->item(0)->isHidden(), "reduced motion hides the excluded row at once");
      check(l2->item(0)->sizeHint().height() == 0, "…with its slot already closed");
      f2->setCurrentIndex(f2->findData(QStringLiteral("all")));
      check(!l2->item(0)->isHidden() &&
                l2->item(0)->sizeHint().height() ==
                    l2->item(0)->data(FILTER_FULL_HEIGHT_ROLE).toInt(),
            "…and restores it whole, still without animating");
      qunsetenv("STENCIL_NO_ANIM");
    }
    mock.mintBearer.clear();
    mock.goodBearer.clear();
    mock2.goodBearer.clear();
    mock.mintToken = "tok";
  }

  std::printf(failures ? "FAILURE (%d failures)\n" : "OK\n", failures);
  return failures ? 1 : 0;
}

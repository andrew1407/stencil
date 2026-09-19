// Classifying a connection: which kind of row a reachable origin yields.
#include "serverAuthParts.hpp"

namespace serverauth {

  void checkClassification(MockServer& mock, MockServer& mock2) {
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
      check(cl->status() == ServerClient::Status::EXPIRED,
            "a refused credential is Expired, not Error");
      check(cl->needsReauth(), "…and says it needs re-authentication");
      check(cl->base() == ServerClient::normalizeBase(mock.url()), "the URL survives");

      // ── no retry loop: further work leaves it expired and mints nothing ──
      const int before = mock.tokenRequests;
      cl->listProjectsAsync([](bool, QVector<stencil::net::ServerProject>) {});
      pumpFor(150);
      check(cl->status() == ServerClient::Status::EXPIRED, "it stays expired");
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
      check(cl->status() != ServerClient::Status::EXPIRED,
            "an unreachable host is not an expired session");
      check(!cl->needsReauth(), "…and offers no re-auth");
    } else {
      check(true, "an unreachable host is not an expired session");
      check(true, "…and offers no re-auth");
    }
    // …and the toast reads like the browser's: a request that never reached the server has no status, so it
    // carries the TRANSPORT's message rather than a desktop-invented "HTTP 0".
    check(!err.contains(QLatin1String("HTTP 0")), "a dead host never reports \"HTTP 0\"");
    check(!err.isEmpty() && !err.contains(QLatin1String("HTTP")),
          "…it reports what the transport said instead");
  }

  // A REST failure reads exactly as the browser words it (connectionManager.js _req): "<METHOD> <path>:
  // HTTP <status>", or the server's own JSON `message`, which the connect toast then wraps.
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
    check(cl && cl->status() == ServerClient::Status::CONNECTED, "…and reads as connected");
    mock.projectsStatus = 401;   // the session lapses on the server
    const int mintsBefore = mock.tokenRequests;
    bool done = false;
    cl->listProjectsAsync([&](bool, QVector<stencil::net::ServerProject>) { done = true; });
    pumpUntil([&] { return done; });
    check(cl->status() == ServerClient::Status::EXPIRED,
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
    check(cl && cl->status() == ServerClient::Status::CONNECTED, "…and lands Connected");
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
    check(cl && cl->status() == ServerClient::Status::CONNECTED && !cl->needsReauth(),
          "…with no re-auth needed");
  }

  }

}  // namespace serverauth

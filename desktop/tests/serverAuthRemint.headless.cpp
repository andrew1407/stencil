// Re-minting mid-session, an expired row, and signing back in with a pasted token.
#include "serverAuthParts.hpp"

namespace serverauth {

  void checkRemintAndExpiry(MockServer& mock, MockServer& mock2) {
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
    check(cl->status() == ServerClient::Status::CONNECTED, "…without ever reading as expired");
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
    check(cl->status() == ServerClient::Status::EXPIRED, "…and lands Expired");
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
    check(cl && cl->status() == ServerClient::Status::EXPIRED, "the row's client is expired");

    ConnectDialog dlg(&mgr);
    dlg.resize(520, 420);
    dlg.show();
    pumpFor(60);
    // No separate note: the browser says it with the amber card and the amber dot, and the desktop says it
    // the same way. The sentence lives on the dot's and the URL's tooltips.
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
      return c && c->status() == ServerClient::Status::CONNECTED;
    });
    ServerClient* after = mgr.find(mock.url());
    check(after && after->status() == ServerClient::Status::CONNECTED,
          "reconnect signs in again without a prompt");
    pumpFor(60);
    check(dlg.findChild<QLabel*>(QStringLiteral("expiredNote")) == nullptr,
          "…and the expired note leaves with it");
  }

  // A pasted token signs the EXPIRED row in. The refused client keeps its place in the list on purpose, so
  // connectToAsync() would answer "already connected"; reauthenticateAsync() reuses the listed client.
  std::printf("expired row: signing in with a pasted token:\n");
  {
    mock.projectsStatus = 401;
    mock.tokenStatus = 401;
    ConnectionManager m2;
    QString err0;
    stencil::test::connectNow(m2, mock.url(), QStringLiteral("stale-token"), err0);
    ServerClient* c = m2.find(mock.url());
    check(c && c->status() == ServerClient::Status::EXPIRED, "an expired row to sign in");

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
    check(back && back->status() == ServerClient::Status::CONNECTED,
          "…and the connection is live again");
    check(m2.urls().size() == 1, "…in the one row it always was");
  }

  }

}  // namespace serverauth

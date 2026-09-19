// Invite links: parsing them, minting one, and the row it produces.
#include "serverAuthParts.hpp"

namespace serverauth {

  void checkInvites(MockServer& mock, MockServer& mock2) {
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
      check(cl->status() == ServerClient::Status::CONNECTED, "…landing Connected");
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
    check(cl2 && cl2->status() == ServerClient::Status::CONNECTED &&
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

  }

}  // namespace serverauth

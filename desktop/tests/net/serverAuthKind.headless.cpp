// The credential kind a row carries, and that it survives a reload.
#include "serverAuthParts.hpp"

namespace serverauth {

  void checkCredentialKind(MockServer& mock, MockServer& mock2) {
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
    check(cl && cl->credentialKind() == ServerClient::CredentialKind::ADMIN,
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
    check(cl && cl->credentialKind() == ServerClient::CredentialKind::SESSION,
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
    check(cl && cl->credentialKind() == ServerClient::CredentialKind::NONE,
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
    check(cl && cl->credentialKind() == ServerClient::CredentialKind::SESSION,
          "…classified Session at connect");
    mock.goodBearer = "sess-q";   // the server restarts: it mints "sess-q" now
    mock.mintToken = "sess-q";
    bool called = false, ok = false;
    cl->listProjectsAsync([&](bool o, QVector<stencil::net::ServerProject>) { ok = o; called = true; });
    pumpUntil([&] { return called; });
    check(ok, "the lapsed session is rescued by the credential");
    check(cl->credentialKind() == ServerClient::CredentialKind::ADMIN,
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
                        ServerClient::CredentialKind::ADMIN),
          "a RESTORED admin credential connects");
    ServerClient* cl = mgr.find(mock.url());
    check(cl && cl->isAdmin() && cl->getToken() == QStringLiteral("sess-h"),
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
      check(ServerClient::kindFromTag(back[0].kind) == ServerClient::CredentialKind::ADMIN &&
                ServerClient::kindFromTag(back[1].kind) == ServerClient::CredentialKind::SESSION,
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
              ServerClient::kindFromTag(legacy[0].kind) == ServerClient::CredentialKind::NONE,
          "…which reads as no kind (it probes, like it always did)");
    // …and only a RECOGNISED trailing tag is a kind, so a token with a tab survives.
    s.setValue(QStringLiteral("connections/servers"),
               QStringList{QStringLiteral("http://old.example:8090\tto\tken")});
    const auto tabbed = stencil::net::connectionStore::loadSavedServers();
    check(tabbed.size() == 1 && tabbed[0].token == QStringLiteral("to\tken") &&
              tabbed[0].kind.isEmpty(),
          "…and a trailing field that is not a kind tag stays part of the token");
  }

  }

}  // namespace serverauth

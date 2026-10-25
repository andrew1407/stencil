// A launch link's token for a saved server (ConnectionManager::connectSessionOnlyAsync): the live
// connection uses it, while snapshot() — what saveKeeping writes — keeps the saved credential. And a
// server named by host name is resolved and judged before its handshake.
#include "serverAuthParts.hpp"

namespace serverauth {

  void checkSessionOnly(MockServer& mock) {
    std::printf("session-only credential:\n");
    mock.goodBearer = "link-tok";
    {
      ConnectionManager mgr;
      bool called = false, ok = false;
      mgr.connectSessionOnlyAsync(mock.url(), QStringLiteral("link-tok"),
                                  {mock.url(), QStringLiteral("saved-tok"), QStringLiteral("admin")},
                                  [&](bool o, QString) { ok = o; called = true; });
      pumpUntil([&] { return called; });
      check(ok, "the link's token connects");
      ServerClient* cl = mgr.find(mock.url());
      check(cl && cl->getCredential() == QStringLiteral("link-tok"), "…and is the live credential");
      const auto snap = mgr.snapshot();
      check(snap.size() == 1 && snap[0].token == QStringLiteral("saved-tok") && snap[0].kind == QStringLiteral("admin"),
            "the snapshot keeps the saved credential and its kind");
      stencil::net::connectionStore::saveServers({{mock.url(), QStringLiteral("saved-tok"), QStringLiteral("admin")}});
      stencil::net::connectionStore::saveKeeping(snap, mgr.getForgotten());
      const auto back = stencil::net::connectionStore::loadSavedServers();
      check(back.size() == 1 && back[0].token == QStringLiteral("saved-tok"), "…so the saved set is unchanged");

      bool again = false;
      mgr.reauthenticateAsync(mock.url(), QStringLiteral("link-tok"), [&](bool, QString) { again = true; });
      pumpUntil([&] { return again; });
      check(mgr.snapshot().value(0).token == QStringLiteral("link-tok"), "a token the user signs in with is kept");
    }
    {
      ConnectionManager mgr;
      bool called = false;
      mgr.connectSessionOnlyAsync(mock.url(), QStringLiteral("wrong-tok"), {mock.url(), QStringLiteral("saved-tok"), {}},
                                  [&](bool, QString) { called = true; });
      pumpUntil([&] { return called; });
      const auto snap = mgr.snapshot();
      check(snap.isEmpty() || snap[0].token == QStringLiteral("saved-tok"),
            "a refused link token never stands in for the saved one");
    }
    {
      // A NAME goes through the resolver before the handshake; loopback is a server a user may name.
      ConnectionManager mgr;
      bool called = false, ok = false;
      const QString named = QStringLiteral("http://localhost:%1").arg(mock.server.serverPort());
      mgr.connectToAsync(named, QStringLiteral("link-tok"), [&](bool o, QString) { ok = o; called = true; });
      pumpUntil([&] { return called; });
      check(ok && mgr.find(named), "a server named by a resolvable name still connects after the lookup");
    }
    mock.goodBearer.clear();
    stencil::net::connectionStore::saveServers({});
  }

}  // namespace serverauth

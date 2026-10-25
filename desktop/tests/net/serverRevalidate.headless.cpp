// The project poll's revalidation (ServerClient::getProjectAsync with a known version): the request
// carries the server's "v<version>" ETag, and a 304 answers unchanged at that version, with no layout
// and no error left on the client.
#include "serverAuthParts.hpp"

namespace serverauth {

  void checkRevalidation(MockServer& mock) {
    std::printf("project revalidation:\n");
    mock.notModified = "\"v7\"";
    ConnectionManager mgr;
    QString err;
    check(stencil::test::connectNow(mgr, mock.url(), QString(), err), "connects");
    ServerClient* c = mgr.find(mock.url());
    bool called = false, ok = false;
    stencil::net::ServerProject meta;
    QJsonObject layout;
    mock.lines.clear();
    c->getProjectAsync(QStringLiteral("p_a_1"), [&](bool o, stencil::net::ServerProject m, QJsonObject l) {
      ok = o;
      meta = m;
      layout = l;
      called = true;
    }, 7);
    pumpUntil([&] { return called; });
    check(ok && meta.version == 7 && layout.isEmpty(), "a 304 reads as unchanged at the known version");
    check(c->lastError().isEmpty(), "…and leaves no error behind");
    mock.notModified.clear();
  }

}  // namespace serverauth

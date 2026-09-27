// The project list walks every page the server hands out (nextCursor → ?after=) against the
// in-process MockRest, and a looping or endless server fails the list instead of hanging it.
#include "ServerClient.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>

#include "../support/check.hpp"
#include "../support/mockRest.hpp"

using stencil::net::ServerClient;
using stencil::net::ServerProject;
using stencil::test::MockRest;

namespace {
  struct Listed {
    bool finished = false;
    bool ok = false;
    QStringList ids;
    QString serverUrl;
  };

  Listed listOnce(MockRest& mock) {
    mock.listGets = 0;
    ServerClient client(mock.url());
    Listed got;
    QEventLoop loop;
    client.listProjectsAsync([&](bool ok, QVector<ServerProject> ps) {
      got.finished = true;
      got.ok = ok;
      for (const ServerProject& p : ps) got.ids << p.id;
      if (!ps.isEmpty()) got.serverUrl = ps.last().serverUrl;
      loop.quit();
    });
    QTimer::singleShot(60000, &loop, &QEventLoop::quit);
    if (!got.finished) loop.exec();
    return got;
  }
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  MockRest mock;
  check(mock.listen(), "the mock server listens");
  for (int i = 1; i <= 7; ++i) mock.projects[QStringLiteral("p%1").arg(i)].name = QStringLiteral("n%1").arg(i);
  const QStringList seven{"p1", "p2", "p3", "p4", "p5", "p6", "p7"};

  std::printf("an unpaged server:\n");
  const Listed whole = listOnce(mock);
  check(whole.ok && whole.ids == seven, "every project comes back");
  check(mock.listGets == 1 && mock.lastAfter.isEmpty(), "in one bare GET /projects, as before paging");

  std::printf("a paging server:\n");
  mock.pageSize = 3;
  const Listed paged = listOnce(mock);
  check(paged.ok && paged.ids == seven, "every page is gathered, in order");
  check(mock.listGets == 3, "seven projects in pages of three take three requests");
  check(paged.serverUrl == whole.serverUrl && !paged.serverUrl.isEmpty(), "a later page's rows carry the server too");
  mock.pageSize = 7;
  const Listed exact = listOnce(mock);
  check(exact.ok && exact.ids == seven && mock.listGets == 2, "a full last page ends on the empty page behind it");

  std::printf("a misbehaving server:\n");
  mock.pageSize = 3;
  mock.stuckCursor = QStringLiteral("17.a/b+c");
  const Listed stuck = listOnce(mock);
  check(stuck.finished && !stuck.ok && stuck.ids.isEmpty(), "a cursor handed back twice fails the list");
  check(mock.listGets == 2, "after asking for the page behind it once");
  check(mock.lastAfter == QLatin1String("17.a/b+c"), "the cursor rides ?after= percent-encoded and arrives whole");
  mock.stuckCursor.clear();
  mock.endlessCursors = true;
  const Listed endless = listOnce(mock);
  check(endless.finished && !endless.ok, "a server that never stops paging fails the list");
  check(mock.listGets == ServerClient::MAX_LIST_PAGES, "after MAX_LIST_PAGES pages");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

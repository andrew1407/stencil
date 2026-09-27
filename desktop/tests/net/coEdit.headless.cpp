// Server-connected headless SMOKE test for the async co-edit round-trip behind openServerProject and
// saveToServer: the open/load path (getProjectAsync + downloadFileAsync), the save path
// (runGuardedWriteAsync with a line-union merge resolve), and convergence — a stale editor's
// concurrent save merges instead of clobbering. MainWindow's canvas adoption stays UI-coupled.
// SELF-SKIPS (exit 0) with no server; point it at one with STENCIL_TEST_SERVER. Needs Qt.
#include "coEditParts.hpp"

#include <QGuiApplication>
#include "../support/connectNow.hpp"

using stencil::net::ConnectionManager;
using namespace coedit;

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);

  const QString serverUrl = qEnvironmentVariableIsSet("STENCIL_TEST_SERVER")
                                ? qEnvironmentVariable("STENCIL_TEST_SERVER")
                                : QStringLiteral("http://localhost:8090");

  // Two independent connections = two editors (each gets its own token).
  ConnectionManager mgrA, mgrB;
  QString errA, errB;
  if (!stencil::test::connectNow(mgrA, serverUrl, QString(), errA) ||
      !stencil::test::connectNow(mgrB, serverUrl, QString(), errB)) {
    std::printf("SKIP: no reachable stencil server at %s (%s / %s)\n",
                serverUrl.toUtf8().constData(), errA.toUtf8().constData(),
                errB.toUtf8().constData());
    return 0;  // self-skip, like the gated transfer / go store integration tests
  }
  ServerClient* A = mgrA.find(serverUrl);
  ServerClient* B = mgrB.find(serverUrl);
  check(A && B, "two editor connections established");
  if (!A || !B) { std::printf("FAILED (%d failures)\n", failures); return 1; }

  QString id;
  qint64 v0 = 0;
  qint64 bVersion = 0;
  if (!openShared(A, B, id, v0, bVersion)) { std::printf("FAILED (%d failures)\n", failures); return 1; }
  saveAndConverge(A, B, id, v0, bVersion);

  std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}

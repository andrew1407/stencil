// Test-count floor for the desktop suite: counts the add_test( lines of ctest's own
// generated CTestTestfile.cmake, so a target that silently stopped being registered
// (the GUI areas share one object library) fails here instead of vanishing from pass/fail.
// QtCore + the filesystem, no display; compiles no app source.
#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QStringList>
#include <cstdio>

#include "support/check.hpp"

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  // A floor, not a pin: adding tests never trips it — raise it as the suite grows.
  const int minTargets = 64;
  QFile ctestFile(QStringLiteral(STENCIL_CTEST_FILE));
  check(ctestFile.open(QIODevice::ReadOnly), "the generated CTestTestfile.cmake opens");
  const QStringList ctestLines =
      QString::fromUtf8(ctestFile.readAll()).split(QLatin1Char('\n'));
  int registered = 0;
  for (const QString& line : ctestLines)
    if (line.startsWith(QStringLiteral("add_test("))) ++registered;
  const QByteArray collapsed =
      QStringLiteral("desktop suite collapsed to %1 ctest targets, floor is %2")
          .arg(registered).arg(minTargets).toUtf8();
  check(registered >= minTargets, collapsed.constData());

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

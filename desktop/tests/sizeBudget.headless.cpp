// Size, comment and folder ratchet for the desktop surface (tests/sizeBudget.json): no new
// oversized .cpp/.hpp under src/ or tests/, no listed file growing past its recorded line
// count, no directory raising its comment share and none past the file-count cap. The budget
// is the committed snapshot of today's tree — the refactor lowers those numbers, never raises
// them. The scanner is sizeBudgetParts.hpp; QtCore + the filesystem, no display needed.
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <cstdio>

#include "sizeBudgetParts.hpp"
#include "support/check.hpp"

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  const QDir root = repoRoot();
  check(root.exists(QStringLiteral("CLAUDE.md")), "repo root found from the binary path");

  QFile budgetFile(root.filePath(QStringLiteral("desktop/tests/sizeBudget.json")));
  check(budgetFile.open(QIODevice::ReadOnly), "sizeBudget.json opens");
  const QJsonObject budget = QJsonDocument::fromJson(budgetFile.readAll()).object();
  check(!budget.isEmpty(), "sizeBudget.json parses");

  const int maxNew = budget.value(QStringLiteral("maxNewFileLines")).toInt();
  const QJsonObject files = budget.value(QStringLiteral("files")).toObject();
  const QJsonObject commentPct = budget.value(QStringLiteral("commentPct")).toObject();
  const QJsonObject exceptions = budget.value(QStringLiteral("exceptions")).toObject();
  const QJsonObject dirBudget = budget.value(QStringLiteral("dirs")).toObject();
  const int maxPerDir = budget.value(QStringLiteral("maxFilesPerDir")).toInt();
  check(maxNew > 0, "maxNewFileLines is set");

  // Scope: .cpp/.hpp under desktop/src and desktop/tests, repo-relative.
  QMap<QString, Counts> measured;
  QMap<QString, Counts> dirs;
  QMap<QString, QSet<QString>> stems;
  for (const QString& base : {QStringLiteral("desktop/src"), QStringLiteral("desktop/tests")}) {
    QDirIterator it(root.filePath(base), {QStringLiteral("*.cpp"), QStringLiteral("*.hpp")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString abs = it.next();
      QFile f(abs);
      if (!f.open(QIODevice::ReadOnly)) continue;
      const QString rel = root.relativeFilePath(abs);
      const Counts c = scanSource(QString::fromUtf8(f.readAll()));
      measured.insert(rel, c);
      Counts& d = dirs[QFileInfo(rel).path()];
      d.total += c.total;
      d.comment += c.comment;
      // A header and its .cpp are one thing to the reader, so the fan-out counts stems.
      stems[QFileInfo(rel).path()].insert(QFileInfo(rel).completeBaseName());
    }
  }
  check(measured.size() > 100, "the desktop sources were found and scanned");

  int unlisted = 0, grew = 0, oversized = 0;
  for (auto it = measured.constBegin(); it != measured.constEnd(); ++it) {
    const QString& rel = it.key();
    const int lines = it.value().total;
    if (exceptions.contains(rel)) continue;  // generated / byte-pinned twins only
    if (!files.contains(rel)) {
      if (lines > maxNew) {
        std::printf("       NEW oversized file: %s (%d lines > %d)\n", qPrintable(rel), lines,
                    maxNew);
        ++unlisted;
      }
      continue;
    }
    const int budgeted = files.value(rel).toInt();
    if (budgeted <= maxNew)
      std::printf("       note: %s is listed but under the limit — drop the entry\n",
                  qPrintable(rel));
    if (lines > budgeted) {
      std::printf("       GREW: %s (%d lines > budgeted %d)\n", qPrintable(rel), lines, budgeted);
      ++grew;
    } else if (lines * 10 < budgeted * 9) {
      std::printf("       note: %s shrank to %d (budget %d) — ratchet it down\n", qPrintable(rel),
                  lines, budgeted);
    }
    ++oversized;
  }
  for (auto it = files.constBegin(); it != files.constEnd(); ++it)
    if (!measured.contains(it.key()))
      std::printf("       note: %s is gone — drop the entry\n", qPrintable(it.key()));

  check(unlisted == 0, "no new file over the line limit");
  check(grew == 0, "no budgeted file grew past its recorded size");
  check(oversized > 0, "the budgeted files still exist");

  int overShare = 0;
  for (auto it = dirs.constBegin(); it != dirs.constEnd(); ++it) {
    if (it.value().total == 0) continue;
    const int pct = it.value().comment * 100 / it.value().total;
    if (!commentPct.contains(it.key())) {
      std::printf("       note: %s (%d%% comments) is not in commentPct\n", qPrintable(it.key()),
                  pct);
      continue;
    }
    const int budgeted = commentPct.value(it.key()).toInt();
    if (pct > budgeted) {
      std::printf("       COMMENT SHARE ROSE: %s at %d%% > budgeted %d%%\n", qPrintable(it.key()),
                  pct, budgeted);
      ++overShare;
    } else if (pct < budgeted) {
      std::printf("       note: %s fell to %d%% (budget %d%%) — ratchet it down\n",
                  qPrintable(it.key()), pct, budgeted);
    }
  }
  check(overShare == 0, "no directory raised its comment share");

  // Folder fan-out: a folder past the cap is split by feature, never recorded here.
  int dirAppeared = 0, dirGrew = 0;
  for (auto it = stems.constBegin(); it != stems.constEnd(); ++it) {
    const int n = it.value().size();
    if (!dirBudget.contains(it.key())) {
      if (n > maxPerDir) {
        std::printf("       NEW oversized folder: %s (%d files > %d) — split it by feature\n",
                    qPrintable(it.key()), n, maxPerDir);
        ++dirAppeared;
      }
      continue;
    }
    const int allowed = dirBudget.value(it.key()).toInt();
    if (n > allowed) {
      std::printf("       %s grew to %d files (budget %d)\n", qPrintable(it.key()), n, allowed);
      ++dirGrew;
    } else if (n < allowed) {
      std::printf("       note: %s fell to %d files (budget %d) — ratchet it down\n",
                  qPrintable(it.key()), n, allowed);
    }
  }
  check(dirAppeared == 0, "no new folder over the file-count cap");
  check(dirGrew == 0, "no budgeted folder grew past its recorded file count");

  // Test-count floor, read from ctest's own generated registry: a target that stopped being registered
  // (the GUI areas share one object library) is invisible to pass/fail — raise it as the suite grows.
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

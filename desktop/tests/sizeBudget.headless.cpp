// Size + comment ratchet for the desktop surface (tests/sizeBudget.json): no new
// oversized .cpp/.hpp under src/ or tests/, no listed file growing past its recorded
// line count, and no directory raising its comment share. The budget is the committed
// snapshot of today's tree — the refactor lowers those numbers, never raises them.
// Pure QtCore + the filesystem; no display needed.
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <cstdio>

#include "support/check.hpp"

namespace {
  struct Counts {
    int total = 0;
    int comment = 0;
  };

  bool identChar(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }

  // Total lines plus the ones that ARE a comment: a line whose first non-blank chars
  // open // or /*, and every line inside an open block comment. String, char and raw
  // literals are skipped, so a // or /* inside one never counts.
  Counts scanSource(const QString& text) {
    QStringList lines = text.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    Counts c;
    c.total = lines.size();
    bool inBlock = false, inRaw = false;
    QString rawDelim;
    for (const QString& line : lines) {
      const int n = line.size();
      bool isComment = inBlock;
      if (!inBlock && !inRaw) {
        int j = 0;
        while (j < n && (line[j] == QLatin1Char(' ') || line[j] == QLatin1Char('\t'))) ++j;
        if (j + 1 < n && line[j] == QLatin1Char('/')
            && (line[j + 1] == QLatin1Char('/') || line[j + 1] == QLatin1Char('*')))
          isComment = true;
      }
      if (isComment) ++c.comment;
      int i = 0;
      while (i < n) {
        if (inRaw) {
          const int k = line.indexOf(QLatin1Char(')') + rawDelim + QLatin1Char('"'), i);
          if (k < 0) break;
          i = k + rawDelim.size() + 2;
          inRaw = false;
          continue;
        }
        if (inBlock) {
          const int k = line.indexOf(QStringLiteral("*/"), i);
          if (k < 0) break;
          i = k + 2;
          inBlock = false;
          continue;
        }
        const QChar ch = line[i];
        if (ch == QLatin1Char('/') && i + 1 < n && line[i + 1] == QLatin1Char('/')) break;
        if (ch == QLatin1Char('/') && i + 1 < n && line[i + 1] == QLatin1Char('*')) {
          inBlock = true;
          i += 2;
          continue;
        }
        // R"delim( … )delim" — the QSS/JSON blobs, which carry their own /* … */.
        if (ch == QLatin1Char('R') && i + 1 < n && line[i + 1] == QLatin1Char('"')
            && (i == 0 || !identChar(line[i - 1])
                || QStringLiteral("LuU8").contains(line[i - 1]))) {
          const int k = line.indexOf(QLatin1Char('('), i + 2);
          if (k < 0) break;
          rawDelim = line.mid(i + 2, k - i - 2);
          inRaw = true;
          i = k + 1;
          continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
          int j = i + 1;
          bool closed = false;
          while (j < n) {
            if (line[j] == QLatin1Char('\\')) {
              j += 2;
              continue;
            }
            if (line[j] == ch) {
              closed = true;
              break;
            }
            ++j;
          }
          i = closed ? j + 1 : n;
          continue;
        }
        ++i;
      }
    }
    return c;
  }

  // Up from the test binary until CLAUDE.md — every path in the budget is relative to
  // that, so the numbers read the same from any build dir.
  QDir repoRoot() {
    QDir d(QCoreApplication::applicationDirPath());
    while (!d.exists(QStringLiteral("CLAUDE.md")) && d.cdUp()) {}
    return d;
  }
}

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
  check(maxNew > 0, "maxNewFileLines is set");

  // Scope: .cpp/.hpp under desktop/src and desktop/tests, repo-relative.
  QMap<QString, Counts> measured;
  QMap<QString, Counts> dirs;
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

  // Test-count floor, read from ctest's own generated registry: a target that stopped
  // being registered (the 15 GUI areas share one object library) is invisible to
  // pass/fail — raise it as the suite grows.
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

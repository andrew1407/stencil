// The size ratchet's source scanner: total lines and the ones that ARE a comment, with
// string, char and raw literals skipped so a // inside one opens nothing. Split out of
// sizeBudget.headless.cpp, which owns the budget comparisons.
#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace {
  struct Counts {
    int total = 0;
    int comment = 0;
  };

  bool identChar(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }

  // Total lines plus the ones that ARE a comment: a line whose first non-blank chars open // or /*, and
  // every line inside an open block comment. String, char and raw literals are skipped.
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

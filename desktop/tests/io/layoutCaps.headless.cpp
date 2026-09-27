// Every layout the desktop reads (server, .stencil, JSON file, session) passes
// fileStore::linesFromJson, bounded by constants.json LIMITS as browser layout.js sanitizeLines
// bounds it: a line keeps its first layoutLinePointsMax points, the line that spends the last of
// layoutPointsMax is cut there and every line after it dropped, and at most layoutLinesMax are read.
#include "fileStore.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

#include "../support/check.hpp"

using stencil::gui::fileStore::linesFromJson;

namespace {
  int limit(const char* key) {
    QFile f(QStringLiteral(":/config/constants.json"));
    if (!f.open(QIODevice::ReadOnly)) return -1;
    return QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("LIMITS")).toObject()
        .value(QLatin1String(key)).toInt(-1);
  }

  QJsonArray pointsOf(int n) {
    QJsonArray pts;
    for (int i = 0; i < n; ++i) pts.append(QJsonObject{{"x", i}, {"y", 1}});
    return pts;
  }

  QJsonObject lineWith(const QJsonArray& pts) { return QJsonObject{{"points", pts}}; }
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const int perLine = limit("layoutLinePointsMax");
  const int total = limit("layoutPointsMax");
  const int maxLines = limit("layoutLinesMax");
  check(perLine > 0 && total > perLine && maxLines > 0, "LIMITS is read from constants.json");
  if (failures) return 1;

  {
    const stencil::core::Lines one = linesFromJson(QJsonArray{lineWith(pointsOf(perLine + 1))});
    check(one.size() == 1 && int(one[0].points.size()) == perLine,
          "a line past layoutLinePointsMax keeps that many points");
    check(!one.empty() && !one[0].points.empty() && one[0].points.back().x == perLine - 1,
          "…its first ones, in order");
  }

  {
    // Lines of three quarters of the per-line cap: the budget runs out part-way through one.
    const int n = perLine * 3 / 4;
    const int whole = total / n;
    const int rest = total - whole * n;
    const QJsonArray pts = pointsOf(n);
    QJsonArray many;
    for (int i = 0; i < whole + 3; ++i) many.append(lineWith(pts));
    const stencil::core::Lines kept = linesFromJson(many);
    qint64 sum = 0;
    for (const auto& l : kept) sum += qint64(l.points.size());
    check(sum == total, "the lines hold exactly layoutPointsMax points");
    check(int(kept.size()) == whole + (rest > 0 ? 1 : 0), "every line after the budget is dropped");
    check(rest == 0 || (!kept.empty() && int(kept.back().points.size()) == rest),
          "the line that spends the last point is cut there");
  }

  {
    QJsonArray many;
    const QJsonObject dot = lineWith(pointsOf(1));
    for (int i = 0; i < maxLines + 5; ++i) many.append(dot);
    check(int(linesFromJson(many).size()) == maxLines, "no more than layoutLinesMax lines are read");
  }

  {
    int w = 0, h = 0;
    const QJsonObject layout{{"lines", QJsonArray{lineWith(pointsOf(perLine + 7))}}};
    const stencil::core::Lines parsed = stencil::gui::fileStore::parseLayoutJson(layout, w, h);
    check(parsed.size() == 1 && int(parsed[0].points.size()) == perLine,
          "a parsed layout document is capped the same way");
  }

  std::printf("%s (%d failure(s))\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}

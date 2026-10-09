// A line's own `hidden` and `name` (core::Line): a layout writes and reads both, a name trimmed and
// capped at LIMITS.lineNameMax; a hidden line keeps its index but is no pointer target. Browser twin:
// browser/tests/core/hiddenLines.test.js.
#include "fileStore.hpp"
#include "lineName.hpp"
#include "markHits.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <cstdio>

#include "../support/check.hpp"

namespace core = stencil::core;
namespace model = stencil::model;
using namespace stencil::gui;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  std::printf("layout:\n");
  core::Line named;
  named.points = {{1, 2}};
  named.name = "Roof ridge";
  named.hidden = true;
  const QJsonObject o = fileStore::lineToJson(named);
  check(o.value("name").toString() == "Roof ridge" && o.value("hidden").toBool(), "both are written when set");
  const core::Line back = fileStore::lineFromJson(o);
  check(back.name == "Roof ridge" && back.hidden, "…and read back");
  const QJsonObject plain = fileStore::lineToJson(core::Line{});
  check(!plain.contains("name") && !plain.contains("hidden"), "an unnamed, shown line writes neither");
  const core::Line capped = fileStore::lineFromJson(
      QJsonObject{{"points", QJsonArray{}}, {"name", "  " + QString(300, 'x') + "  "}, {"hidden", 7}});
  check(model::lineNameMax() == 80 && capped.name == std::string(80, 'x'), "a name is trimmed and capped");
  check(!capped.hidden, "a hidden flag that is no bool reads as shown");
  check(model::lineNameOf(QStringLiteral("  a b ")) == "a b", "lineNameOf trims");

  std::printf("hits:\n");
  core::Line under, over;
  under.points = over.points = {{0, 0}, {100, 0}};
  over.hidden = true;
  const core::Lines lines{under, over};
  const model::ShownMarks both{true, true};
  core::Lines scratch;
  check(&model::hittable(core::Lines{under}, scratch) != &scratch, "none hidden: the lines themselves");
  check(model::hittable(lines, scratch).size() == 2 && scratch[1].points.empty(), "a hidden line keeps its index");
  check(model::lineAt(lines, both, 50, 1, 8) == 0, "the shown line under a hidden one answers");
  check(model::segmentAt(lines, both, 50, 1, 12)->lineIdx == 0, "…as a segment");
  check(model::pointAt(lines, both, 0, 0, 12)->lineIdx == 0, "…and as a point");
  check(model::holdTargetAt(lines, both, 50, 1, 12).lineIdx == 0, "…and as a hold target");
  const core::Lines gone{over};
  check(model::lineAt(gone, both, 50, 1, 8) == -1 && !model::pointAt(gone, both, 0, 0, 12),
        "a hidden line alone is never hit");

  std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
  return failures ? 1 : 0;
}

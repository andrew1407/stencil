// The pointer's hit tests over what the scene draws (model/markHits.hpp over core hitTest): hidden
// points are never a point target, hidden lines never a segment target, and with both hidden nothing
// is. Browser twin: browser/tests/core/pointer/markHits.test.js.
#include "markHits.hpp"

#include <cstdio>
#include <utility>

#include "../support/check.hpp"

namespace core = stencil::core;
namespace model = stencil::model;

int main() {
  core::Line corner, lone;
  corner.points = {{0, 0}, {100, 0}, {100, 100}};
  lone.points = {{300, 300}};
  const core::Lines lines{corner, lone};
  const model::ShownMarks both{true, true}, strokes{false, true}, dots{true, false}, none{false, false};

  std::printf("lineAt:\n");
  const std::pair<double, double> spots[] = {{50, 5}, {110, -5}, {301, 301}, {200, 200}};
  for (const auto& [x, y] : spots)
    check(model::lineAt(lines, both, x, y, 8) == core::findLineAt(lines, x, y, 8), "both shown: core findLineAt itself");
  check(model::lineAt(lines, both, 110, -5, 8) == 0, "a shown vertex reaches past its stroke (radius + 4)");
  check(model::lineAt(lines, strokes, 50, 5, 8) == 0, "points hidden: the stroke still answers");
  check(model::lineAt(lines, strokes, 110, -5, 8) == -1, "…but a hidden vertex reaches no further than it");
  check(model::lineAt(lines, strokes, 301, 301, 8) == -1, "…and a lone point, drawn as nothing, is never hit");
  check(model::lineAt(lines, dots, 50, 3, 8) == -1, "lines hidden: a stroke body is empty canvas");
  check(model::lineAt(lines, dots, 103, -4, 8) == 0, "…a shown point names its line");
  check(model::lineAt(lines, dots, 301, 301, 8) == 1, "…and so does a lone one");
  check(model::lineAt(lines, none, 100, 0, 8) == -1 && model::lineAt(lines, none, 300, 300, 8) == -1,
        "both hidden: nothing is hit");

  std::printf("points and segments:\n");
  check(model::pointAt(lines, both, 100, 2, 12).has_value(), "a shown point is a target");
  check(!model::pointAt(lines, strokes, 100, 2, 12) && !model::pointAt(lines, none, 100, 2, 12),
        "a hidden one is not");
  check(model::pointIn(corner.points, both, 99, 1, 12) == 1, "the in-progress stroke's vertex, shown");
  check(!model::pointIn(corner.points, strokes, 99, 1, 12), "…and hidden");
  check(model::segmentAt(lines, dots, 50, 3, 12) == std::nullopt, "a hidden stroke is no segment target");
  check(model::segmentAt(lines, strokes, 50, 3, 12).has_value(), "a shown one is");

  std::printf("hold-to-draw:\n");
  const auto hold = [&](model::ShownMarks shown, double x, double y) {
    return model::holdTargetAt(lines, shown, x, y, 12).kind;
  };
  using K = core::HoldTargetKind;
  check(hold(both, 102, 1) == K::CONTINUE_POINT, "a shown point is continued");
  check(hold(strokes, 102, 1) == K::INSERT_SEGMENT, "a hidden vertex leaves its shown stroke as the target");
  check(hold(strokes, 301, 301) == K::NEW_LINE, "a hidden lone point is empty canvas");
  check(hold(both, 50, 3) == K::INSERT_SEGMENT && hold(dots, 50, 3) == K::NEW_LINE,
        "a hidden stroke is never inserted into");
  check(hold(dots, 102, 1) == K::CONTINUE_POINT && hold(none, 102, 1) == K::NEW_LINE,
        "shown points are continued; with nothing shown a hold starts afresh");

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}

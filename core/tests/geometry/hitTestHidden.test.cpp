#include "doctest.h"
#include "hitTest.hpp"
#include <vector>

using namespace stencil::core;

// A hidden line offers no mark: every finder over the lines as drawn must answer what it answered
// over the old blank stand-in (each hidden line swapped for a mark-less one, indices kept).
// Mirrors browser/tests/core/hitTestHidden.test.js.

namespace {
  Lines blanked(const Lines& lines) {
    Lines out;
    for (const Line& l : lines) out.push_back(l.hidden ? Line{} : l);
    return out;
  }

  Line line(std::vector<Point> pts, bool hidden) {
    Line l;
    l.points = std::move(pts);
    l.hidden = hidden;
    return l;
  }

  bool same(const std::optional<PointHit>& a, const std::optional<PointHit>& b) {
    return a.has_value() == b.has_value() && (!a || (a->lineIdx == b->lineIdx && a->ptIdx == b->ptIdx));
  }
  bool same(const std::optional<SegmentHit>& a, const std::optional<SegmentHit>& b) {
    return a.has_value() == b.has_value() &&
           (!a || (a->lineIdx == b->lineIdx && a->ptIdx1 == b->ptIdx1 && a->ptIdx2 == b->ptIdx2));
  }
  bool same(const HoldTarget& a, const HoldTarget& b) {
    return a.kind == b.kind && a.lineIdx == b.lineIdx && a.ptIdx == b.ptIdx && a.ptIdx2 == b.ptIdx2;
  }
}  // namespace

TEST_CASE("a hidden line is hit exactly as its old blank stand-in was, on every side of the stack") {
  // Three lines over one stroke, so each finder has a topmost to pick; one apart, one crossing.
  const std::vector<Point> stroke{{0, 0}, {100, 0}};
  const std::vector<Point> near{{0, 3}, {100, 3}};
  const std::vector<Point> apart{{0, 60}, {100, 60}};
  const std::vector<Point> cross{{50, -40}, {50, 40}};
  std::vector<Lines> stacks;
  for (int mask = 0; mask < 16; ++mask)
    stacks.push_back({line(stroke, mask & 1), line(near, mask & 2), line(apart, mask & 4), line(cross, mask & 8)});
  stacks.push_back({});
  stacks.push_back({line({}, true), line(stroke, false)});

  const std::vector<Point> probes{{0, 0}, {50, 1}, {50, 2}, {100, 3}, {50, 60}, {50, -30}, {200, 200}, {-5, 0}};
  int compared = 0;
  for (const Lines& lines : stacks) {
    const Lines old = blanked(lines);
    for (const Point& p : probes)
      for (const double r : {0.0, 2.5, 8.0, 12.0}) {
        CHECK(findLineAt(lines, p.x, p.y, r) == findLineAt(old, p.x, p.y, r));
        CHECK(same(findNearestPoint(lines, p.x, p.y, r), findNearestPoint(old, p.x, p.y, r)));
        CHECK(same(findNearestSegment(lines, p.x, p.y, r), findNearestSegment(old, p.x, p.y, r)));
        CHECK(same(holdDrawTarget(lines, p.x, p.y, r, r), holdDrawTarget(old, p.x, p.y, r, r)));
        ++compared;
      }
  }
  CHECK(compared == 18 * 8 * 4);
}

TEST_CASE("a hidden line contributes nothing and the indices stay the lines' own") {
  const Lines lines{line({{0, 0}, {100, 0}}, false), line({{0, 0}, {100, 0}}, true)};
  CHECK(findLineAt(lines, 50, 1, 8) == 0);
  CHECK(findNearestPoint(lines, 0, 0, 12)->lineIdx == 0);
  CHECK(findNearestSegment(lines, 50, 1, 12)->lineIdx == 0);
  CHECK(holdDrawTarget(lines, 50, 1).lineIdx == 0);
  const Lines allHidden{line({{0, 0}, {100, 0}}, true)};
  CHECK(findLineAt(allHidden, 50, 1, 8) == -1);
  CHECK(!findNearestPoint(allHidden, 0, 0, 12));
  CHECK(holdDrawTarget(allHidden, 0, 0).kind == HoldTargetKind::NEW_LINE);
}

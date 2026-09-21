#include "doctest.h"
#include "hitTest.hpp"
#include <cmath>
#include <vector>

using namespace stencil::core;

// Mirrors the hit-test cases in browser/tests/geometry.test.js.

// S2: the close-shape predicate ported from drawingApp.js #closeCurrentShape.
TEST_CASE("shouldCloseShape needs >=3 points and a click near point[0]") {
  std::vector<Point> two{{0, 0}, {10, 0}};
  std::vector<Point> tri{{0, 0}, {10, 0}, {10, 10}};
  const double pointSize = 4.0;  // threshold = pointSize + 8 = 12 px
  CHECK_FALSE(shouldCloseShape(two, {0, 0}, pointSize));   // < 3 points
  CHECK(shouldCloseShape(tri, {5, 0}, pointSize));         // within 12 px
  CHECK(shouldCloseShape(tri, {12, 0}, pointSize));        // exactly at threshold
  CHECK_FALSE(shouldCloseShape(tri, {13, 0}, pointSize));  // beyond threshold
  CHECK_FALSE(shouldCloseShape(tri, {30, 30}, pointSize));
}

// S1: hit-testing ported from drawingApp.js:1527-1542 findLineAt.
TEST_CASE("findLineAt returns -1 on empty and topmost (last) line wins") {
  Lines empty;
  CHECK(findLineAt(empty, 0, 0) == -1);

  // Two overlapping horizontal segments; reverse iteration picks the last one.
  Lines lines;
  lines.push_back(Line{{{0, 0}, {10, 0}}});  // index 0
  lines.push_back(Line{{{0, 0}, {10, 0}}});  // index 1 (topmost)
  CHECK(findLineAt(lines, 5, 0) == 1);

  // A point far from every line yields no hit.
  CHECK(findLineAt(lines, 100, 100) == -1);
}

TEST_CASE("findLineAt: point radius threshold+4 vs segment threshold") {
  // Single segment from (0,0) to (20,0); default threshold = 8.
  Lines lines;
  lines.push_back(Line{{{0, 0}, {20, 0}}});

  // Mid-segment perpendicular distance: hit at threshold, miss just beyond.
  CHECK(findLineAt(lines, 10, 8) == 0);     // distToSegment == 8 == threshold
  CHECK(findLineAt(lines, 10, 9) == -1);    // 9 > 8 segment threshold

  // Near an endpoint the wider point radius (threshold + 4 = 12) applies.
  CHECK(findLineAt(lines, 0, 12) == 0);     // hypot == 12 == threshold + 4
  CHECK(findLineAt(lines, 0, 13) == -1);    // 13 > 12 point radius
}

// Move features: nearest-point hit-testing (dragging a point) — port of
// drawingApp.js #findNearestPointWithIdx.
TEST_CASE("findNearestPoint returns topmost line's point within threshold") {
  Lines empty;
  CHECK_FALSE(findNearestPoint(empty, 0, 0).has_value());

  Lines lines;
  lines.push_back(Line{{{0, 0}, {100, 0}}});           // index 0
  lines.push_back(Line{{{10, 10}, {10, 10}, {50, 5}}});  // index 1 (topmost)

  // Exactly on a point -> that point.
  auto a = findNearestPoint(lines, 100, 0);
  REQUIRE(a.has_value());
  CHECK(a->lineIdx == 0);
  CHECK(a->ptIdx == 1);

  // Two lines have a point near (10,10): reverse iteration prefers the topmost.
  auto b = findNearestPoint(lines, 11, 11);
  REQUIRE(b.has_value());
  CHECK(b->lineIdx == 1);
  CHECK(b->ptIdx == 0);

  // Just inside the default threshold (12) of point (50,5).
  CHECK(findNearestPoint(lines, 50, 16).has_value());   // dist 11 < 12
  CHECK_FALSE(findNearestPoint(lines, 50, 18).has_value());  // dist 13 >= 12

  // Nothing near (200,200).
  CHECK_FALSE(findNearestPoint(lines, 200, 200).has_value());
}

// Move features: nearest-segment hit-testing (dragging a segment) — port of
// drawingApp.js #findNearestSegmentWithIdx.
TEST_CASE("findNearestSegment returns the closest segment within threshold") {
  Lines lines;
  lines.push_back(Line{{{0, 0}, {100, 0}, {100, 100}}});  // two segments

  auto s = findNearestSegment(lines, 50, 5);  // closest to the first segment
  REQUIRE(s.has_value());
  CHECK(s->lineIdx == 0);
  CHECK(s->ptIdx1 == 0);
  CHECK(s->ptIdx2 == 1);

  // Closest to the vertical second segment.
  auto v = findNearestSegment(lines, 95, 50);
  REQUIRE(v.has_value());
  CHECK(v->ptIdx1 == 1);
  CHECK(v->ptIdx2 == 2);

  // Beyond threshold yields nothing; a single isolated point has no segment.
  CHECK_FALSE(findNearestSegment(lines, 50, 40).has_value());
  Lines onePoint;
  onePoint.push_back(Line{{{0, 0}}});
  CHECK_FALSE(findNearestSegment(onePoint, 0, 0).has_value());
}

// Single-line nearest-point scan — the in-progress currentLine cursor scan
// shared by canvasWidget mousePress (point grab) and updateHover.
TEST_CASE("nearestPointInLine returns the first point within threshold") {
  std::vector<Point> empty;
  CHECK_FALSE(nearestPointInLine(empty, 0, 0).has_value());

  std::vector<Point> pts{{0, 0}, {50, 0}, {100, 0}};

  // A point exactly at the cursor -> its index (here index 0).
  auto at = nearestPointInLine(pts, 0, 0);
  REQUIRE(at.has_value());
  CHECK(*at == 0);

  // FIRST index within threshold wins (not the nearest): two points qualify
  // for a cursor between them; the lower index is returned.
  std::vector<Point> twoNear{{0, 0}, {5, 0}};
  auto first = nearestPointInLine(twoNear, 4, 0);  // both within 12
  REQUIRE(first.has_value());
  CHECK(*first == 0);

  // Strict `<` boundary mirrors findNearestPoint: dist 11 < 12 hits, dist == 12
  // (and beyond) misses with the default threshold.
  std::vector<Point> one{{0, 0}};
  CHECK(nearestPointInLine(one, 0, 11).has_value());        // dist 11 < 12
  CHECK_FALSE(nearestPointInLine(one, 0, 12).has_value());  // dist 12 not < 12
  CHECK_FALSE(nearestPointInLine(one, 0, 13).has_value());  // dist 13 >= 12

  // A custom threshold arg is honored.
  CHECK(nearestPointInLine(one, 0, 12, 20.0).has_value());        // 12 < 20
  CHECK_FALSE(nearestPointInLine(one, 0, 12, 5.0).has_value());   // 12 not < 5
}


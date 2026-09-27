// Port of the pure half of browser/tests/core/line/chainEdit.test.js: the ring, unchaining and
// pulling a new point out of a line or an area (geometry/lineChain).
#include "doctest.h"
#include "lineChain.hpp"

#include <vector>

using namespace stencil::core;
using namespace stencil::core::chain;

namespace {
  using Pts = std::vector<Point>;

  bool same(const Pts& a, const Pts& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
      if (a[i].x != b[i].x || a[i].y != b[i].y) return false;
    return true;
  }
  // A shape closed by clicking its first point: the closing DUPLICATE at the end.
  Line closedShape() {
    Line l;
    l.locked = true;
    l.points = {{0, 0}, {10, 0}, {10, 10}, {0, 0}};
    return l;
  }
  // A rect: locked, four corners, no duplicate.
  Line rect() {
    Line l;
    l.locked = true;
    l.points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    return l;
  }
}  // namespace

TEST_CASE("ringPoints drops the closing duplicate, and leaves a rect alone") {
  CHECK(ringPoints(closedShape().points).size() == 3);
  CHECK(ringPoints(rect().points).size() == 4);
  CHECK(same(ringPoints({{1, 2}, {3, 4}}), {{1, 2}, {3, 4}}));
  CHECK(ringPoints({}).empty());
}

TEST_CASE("openRingAt re-roots the ring so the seam is where you pulled") {
  CHECK(same(openRingAt(closedShape().points, 1), {{10, 0}, {10, 10}, {0, 0}, {10, 0}}));
  CHECK(same(openRingAt(closedShape().points, 0), {{0, 0}, {10, 0}, {10, 10}, {0, 0}}));
  CHECK(same(openRingAt(rect().points, 3), {{0, 10}, {0, 0}, {10, 0}, {10, 10}, {0, 10}}));
  CHECK(same(openRingAt(rect().points, -1), openRingAt(rect().points, 3)));
}

TEST_CASE("unchainLine turns an area back into an open line and clears its fill") {
  Line shape = closedShape();
  shape.fillColor = "#3399ff";
  CHECK(unchainLine(shape));
  CHECK_FALSE(shape.locked);
  CHECK(same(shape.points, {{0, 0}, {10, 0}, {10, 10}}));
  CHECK(shape.fillColor == "transparent");
  Line r = rect();
  CHECK(unchainLine(r));
  CHECK(r.points.size() == 4);
  Line open;
  open.points = {{0, 0}, {1, 1}};
  CHECK_FALSE(unchainLine(open));
}

TEST_CASE("pullOutPoint on an open line: a vertex is duplicated, a segment gets the cursor") {
  Line line;
  line.points = {{0, 0}, {10, 0}, {20, 0}};
  CHECK(pullOutPoint(line, {true, 1}, 11, 4) == 2);
  CHECK(same(line.points, {{0, 0}, {10, 0}, {10, 0}, {20, 0}}));
  Line seg;
  seg.points = {{0, 0}, {20, 0}};
  CHECK(pullOutPoint(seg, {false, 1}, 9, 5) == 1);
  CHECK(same(seg.points, {{0, 0}, {9, 5}, {20, 0}}));
}

TEST_CASE("pullOutPoint on an area breaks it open where it was pulled") {
  Line shape = closedShape();
  shape.fillColor = "#3399ff";
  const int idx = pullOutPoint(shape, {true, 1}, 11, 4);
  CHECK_FALSE(shape.locked);
  CHECK(shape.fillColor == "transparent");
  CHECK(idx == static_cast<int>(shape.points.size()) - 1);
  CHECK(same(shape.points, {{10, 0}, {10, 10}, {0, 0}, {10, 0}}));
  Line r = rect();
  const int end = pullOutPoint(r, {false, 2}, 14, 6);
  CHECK(r.points[static_cast<std::size_t>(end)].x == 14);
  CHECK(r.points[static_cast<std::size_t>(end)].y == 6);
  CHECK(same(Pts(r.points.begin(), r.points.begin() + end), {{10, 10}, {0, 10}, {0, 0}, {10, 0}}));
}

TEST_CASE("pullOutPoint declines when there is nothing under the cursor") {
  Line one;
  one.points = {{0, 0}};
  CHECK(pullOutPoint(one, {true, 7}, 0, 0) == -1);
  CHECK(pullOutPoint(one, {true, -1}, 0, 0) == -1);
  CHECK(pullOutPoint(one, {false, 2}, 0, 0) == -1);
  CHECK(same(one.points, {{0, 0}}));
}

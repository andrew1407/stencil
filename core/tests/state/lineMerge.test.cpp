// Port of the mergeLines cases in browser/tests/core/layout.test.js and draw/pointColor.test.js:
// the co-edit union merge keyed by lineDedupeKey (state/lineMerge).
#include "doctest.h"

#include <cmath>
#include "lineMerge.hpp"

using namespace stencil::core;

namespace {
  Line at(double x, double y, const char* color) {
    Line l;
    l.points = {{x, y}};
    l.color = color;
    return l;
  }
}  // namespace

TEST_CASE("mergeLines unions distinct lines, the peer's first") {
  const Line a = at(0, 0, "#f00"), shared = at(1, 1, "#0f0"), b = at(2, 2, "#00f");
  const Lines merged = mergeLines({shared, a}, {shared, b});
  REQUIRE(merged.size() == 3);
  CHECK(merged[0].color == "#0f0");
  CHECK(merged[1].color == "#f00");
  CHECK(merged[2].color == "#00f");
  CHECK(mergeLines({a}, {a}).size() == 1);
  CHECK(mergeLines({}, {a}).size() == 1);
  CHECK(mergeLines({a}, {}).size() == 1);
  CHECK(mergeLines({}, {}).empty());
}

TEST_CASE("mergeLines drops a local duplicate of a local line, but keeps the peer's own twins") {
  const Line a = at(0, 0, "#f00");
  CHECK(mergeKeep({}, {a, a}) == std::vector<bool>{true, false});
  CHECK(mergeLines({a, a}, {}).size() == 2);
}

TEST_CASE("lineDedupeKey matches exactly when JS String(n) keys do") {
  Line l;
  l.points = {{1, 2}, {0.1 + 0.2, -0.0}};
  l.color = "#f00";
  l.pointColor = "#00f";
  l.thickness = 2.5;
  l.pointSize = 4;
  l.locked = true;
  Line same = l;
  same.points[1].y = 0.0;  // String(-0) === String(0)
  CHECK(lineDedupeKey(l) == lineDedupeKey(same));
  Line nearby = l;
  nearby.points[1].x = 0.3;  // String(0.1 + 0.2) !== String(0.3)
  CHECK(lineDedupeKey(l) != lineDedupeKey(nearby));
  Line otherField = l;
  otherField.pointColor = "#00e";
  CHECK(lineDedupeKey(l) != lineDedupeKey(otherField));
  Line nanA = l, nanB = l;
  nanA.thickness = std::nan("1");
  nanB.thickness = -std::nan("2");  // String(NaN) is one key whatever the payload
  CHECK(lineDedupeKey(nanA) == lineDedupeKey(nanB));
}

TEST_CASE("mergeLines keys points at full precision: lines a millionth apart stay distinct") {
  // QString::arg(double) keys at 6 significant digits and merged these two.
  const Line a = at(123456.1, 7, "#f00"), b = at(123456.2, 7, "#f00");
  CHECK(mergeLines({a}, {b}).size() == 2);
}

TEST_CASE("mergeLines keeps two lines that differ only by pointColor, and '' is the unset one") {
  Line blue = at(1, 1, "#f00"), green = blue, unset = blue;
  blue.pointColor = "#0000FF";
  green.pointColor = "#00FF00";
  CHECK(mergeLines({blue}, {green}).size() == 2);
  CHECK(mergeLines({unset}, {Line(unset)}).size() == 1);
}

#include "doctest.h"
#include "pointMath.hpp"
#include <cmath>
#include <vector>

using namespace stencil::core;

// Mirrors browser/tests/geometry.test.js.

TEST_CASE("zero-length segment returns distance to the point") {
  Point a{5, 5};
  Point b{5, 5};
  CHECK(distToSegment(8, 9, a, b) == doctest::Approx(std::hypot(3.0, 4.0)));  // 5
}

TEST_CASE("point exactly on segment is ~0") {
  Point a{0, 0};
  Point b{10, 0};
  CHECK(distToSegment(5, 0, a, b) < 1e-9);
}

TEST_CASE("perpendicular distance to mid-segment") {
  Point a{0, 0};
  Point b{10, 0};
  CHECK(distToSegment(5, 4, a, b) == doctest::Approx(4.0));
}

TEST_CASE("projection beyond endpoint a (t clamped to 0)") {
  Point a{0, 0};
  Point b{10, 0};
  CHECK(distToSegment(-3, 0, a, b) == doctest::Approx(3.0));
}

TEST_CASE("projection beyond endpoint b (t clamped to 1)") {
  Point a{0, 0};
  Point b{10, 0};
  CHECK(distToSegment(14, 0, a, b) == doctest::Approx(4.0));
}

// Rotation math + pivots — port of #rotateSelectedLine.
TEST_CASE("rotatePoints rotates about a pivot by the standard matrix") {
  std::vector<Point> pts{{1, 0}, {0, 1}};
  rotatePoints(pts, 0, 0, M_PI / 2);  // +90deg about origin
  CHECK(pts[0].x == doctest::Approx(0.0));
  CHECK(pts[0].y == doctest::Approx(1.0));
  CHECK(pts[1].x == doctest::Approx(-1.0));
  CHECK(pts[1].y == doctest::Approx(0.0));

  // A point at the pivot is unmoved.
  std::vector<Point> atPivot{{5, 5}};
  rotatePoints(atPivot, 5, 5, 1.234);
  CHECK(atPivot[0].x == doctest::Approx(5.0));
  CHECK(atPivot[0].y == doctest::Approx(5.0));
}

// Mirror math — port of #flipSelectedLine.
TEST_CASE("flipPoints reflects each point about the pivot on one axis") {
  // Horizontal flip about cx=5: x=2 -> 8, x=8 -> 2; y is untouched.
  std::vector<Point> h{{2, 3}, {8, 7}};
  flipPoints(h, true, 5, 100);
  CHECK(h[0].x == doctest::Approx(8.0));
  CHECK(h[0].y == doctest::Approx(3.0));
  CHECK(h[1].x == doctest::Approx(2.0));
  CHECK(h[1].y == doctest::Approx(7.0));

  // Vertical flip about cy=5: y=2 -> 8, y=8 -> 2; x is untouched.
  std::vector<Point> v{{3, 2}, {7, 8}};
  flipPoints(v, false, 100, 5);
  CHECK(v[0].x == doctest::Approx(3.0));
  CHECK(v[0].y == doctest::Approx(8.0));
  CHECK(v[1].x == doctest::Approx(7.0));
  CHECK(v[1].y == doctest::Approx(2.0));

  // A point on the pivot axis is unmoved.
  std::vector<Point> onAxis{{5, 9}};
  flipPoints(onAxis, true, 5, 0);
  CHECK(onAxis[0].x == doctest::Approx(5.0));
  CHECK(onAxis[0].y == doctest::Approx(9.0));
}

TEST_CASE("boundingBoxCenter returns the bbox midpoint") {
  std::vector<Point> pts{{0, 0}, {10, 0}, {10, 20}, {0, 20}};
  const Point c = boundingBoxCenter(pts);
  CHECK(c.x == doctest::Approx(5.0));
  CHECK(c.y == doctest::Approx(10.0));

  CHECK(boundingBoxCenter({}).x == doctest::Approx(0.0));
}

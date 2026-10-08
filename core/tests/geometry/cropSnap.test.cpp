// Port of browser/tests/core/parse/cropSnap.test.js: the Math.round commit snap and one
// quarter-turn of the whole edit (geometry/cropSnap).
#include "cropSnap.hpp"
#include "doctest.h"

using namespace stencil::core;

namespace {
  bool same(const CropRect& a, const CropRect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
  }
}  // namespace

TEST_CASE("snapCropRect rounds each side into the image, then moves the origin inside") {
  CHECK(same(snapCropRect({-5, -5, 999, 999}, 200, 100), {0, 0, 200, 100}));
  CHECK(same(snapCropRect({190.4, 2.6, 20.5, 10.2}, 200, 100), {179, 3, 21, 10}));
  CHECK(same(snapCropRect({10.5, 0, 0.2, 2.5}, 200, 100), {11, 0, 1, 3}));
  CHECK(same(snapCropRect({3, 3, 9, 9}, 0.5, 3), {0, 0, 1, 3}));
  // Math.round, not std::round: -2.5 -> -2 and 0.49999999999999994 -> 0.
  CHECK(same(snapCropRect({-2.5, 0.49999999999999994, 10, 10}, 200, 100), {0, 0, 10, 10}));
  CHECK(snapCropRect({2.5, 2.5, 10, 10}, 200, 100).x == 3);
}

TEST_CASE("rotateEditQuarter carries the window into the turned space and wraps the count") {
  const CropRect r{10, 20, 80, 40};
  EditTurn t = rotateEditQuarter(r, 0, 200, 100, true);
  CHECK(same(t.crop, {40, 10, 40, 80}));
  CHECK(t.quarters == 1);
  t = rotateEditQuarter(r, 0, 200, 100, false);
  CHECK(same(t.crop, {20, 110, 40, 80}));
  CHECK(t.quarters == 3);
  t = rotateEditQuarter(CropRect{5, 10, 40, 80}, 1, 200, 100, true);
  CHECK(same(t.crop, {110, 5, 80, 40}));
  CHECK(t.quarters == 2);
  t = EditTurn{r, 0};
  for (int i = 0; i < 4; ++i) t = rotateEditQuarter(t.crop, t.quarters, 200, 100, true);
  CHECK(same(t.crop, r));
  CHECK(t.quarters == 0);
}

TEST_CASE("rotateEditQuarter snaps a fractional window after the turn") {
  const EditTurn t = rotateEditQuarter(CropRect{0.4, 7.6, 33.5, 90}, 0, 200, 100, true);
  CHECK(same(t.crop, {2, 0, 90, 34}));
  CHECK(t.quarters == 1);
}

TEST_CASE("rotateEditQuarter turns the crop-local lines inside the OLD window") {
  Lines lines(1);
  lines[0].points = {{0, 0}, {80, 40}};
  rotateEditQuarter(lines, CropRect{10, 20, 80, 40}, 0, 200, 100, true);
  CHECK(lines[0].points[0].x == 40);
  CHECK(lines[0].points[0].y == 0);
  CHECK(lines[0].points[1].x == 0);
  CHECK(lines[0].points[1].y == 80);
}

TEST_CASE("mirrorEdit reflects the window across the turned width and negates the count") {
  const CropRect r{10, 20, 80, 40};
  EditTurn t = mirrorEdit(r, 0, 200, 100);
  CHECK(same(t.crop, {110, 20, 80, 40}));
  CHECK(t.quarters == 0);
  t = mirrorEdit(CropRect{5, 10, 40, 80}, 1, 200, 100);
  CHECK(same(t.crop, {55, 10, 40, 80}));
  CHECK(t.quarters == 3);
  t = mirrorEdit(t.crop, t.quarters, 200, 100);
  CHECK(same(t.crop, {5, 10, 40, 80}));
  CHECK(t.quarters == 1);
}

TEST_CASE("mirrorEdit then a clockwise turn equals a counter-clockwise turn then mirrorEdit") {
  const CropRect r{10, 20, 80, 40};
  const EditTurn a = mirrorEdit(r, 0, 200, 100);
  const EditTurn a2 = rotateEditQuarter(a.crop, a.quarters, 200, 100, true);
  const EditTurn b = rotateEditQuarter(r, 0, 200, 100, false);
  const EditTurn b2 = mirrorEdit(b.crop, b.quarters, 200, 100);
  CHECK(same(a2.crop, b2.crop));
  CHECK(a2.quarters == b2.quarters);
}

TEST_CASE("mirrorEdit mirrors the crop-local lines inside the window") {
  Lines lines(1);
  lines[0].points = {{0, 5}, {30, 40}};
  mirrorEdit(lines, CropRect{10, 20, 80, 40}, 0, 200, 100);
  CHECK(lines[0].points[0].x == 80);
  CHECK(lines[0].points[0].y == 5);
  CHECK(lines[0].points[1].x == 50);
  CHECK(lines[0].points[1].y == 40);
}

#include "doctest.h"
#include "cropGeometry.hpp"

using namespace stencil::core;

namespace {
  // A3 in cm; aspect ratio ≈ √2. Used as the page in most cases below.
  constexpr double kA3W = 29.7;
  constexpr double kA3H = 42.0;
}  // namespace

TEST_CASE("isAlbumOrientation is wider-than-tall") {
  CHECK(isAlbumOrientation(200, 100));
  CHECK_FALSE(isAlbumOrientation(100, 200));
  CHECK_FALSE(isAlbumOrientation(100, 100));  // square is not album
}

TEST_CASE("cropAspect picks width/height for the orientation, ignoring page order") {
  // Portrait: short / long ≈ 1/√2 ≈ 0.707; album: long / short ≈ √2 ≈ 1.414.
  CHECK(cropAspect(kA3W, kA3H, false) == doctest::Approx(29.7 / 42.0));
  CHECK(cropAspect(kA3W, kA3H, true) == doctest::Approx(42.0 / 29.7));
  // A page given album-first yields the same proportions.
  CHECK(cropAspect(kA3H, kA3W, true) == doctest::Approx(42.0 / 29.7));
  CHECK(cropAspect(0, 0, true) == doctest::Approx(1.0));  // degenerate guard
}

TEST_CASE("centeredCrop cuts the surplus height of a tall portrait image") {
  // Spec example: a 100x200 image at A3 (portrait) → 100x141, cutting top+bottom.
  const auto r = centeredCrop(100, 200, cropAspect(kA3W, kA3H, false));
  CHECK(r.width == doctest::Approx(100.0));
  CHECK(r.height == doctest::Approx(100.0 * 42.0 / 29.7));  // ≈ 141.4
  CHECK(r.x == doctest::Approx(0.0));
  CHECK(r.y == doctest::Approx((200.0 - r.height) / 2.0));  // centered vertically
}

TEST_CASE("centeredCrop cuts the surplus width of a wide album image") {
  // Spec example: a 200x100 image at A3 (album) → 141x100, cutting left+right.
  const auto r = centeredCrop(200, 100, cropAspect(kA3W, kA3H, true));
  CHECK(r.height == doctest::Approx(100.0));
  CHECK(r.width == doctest::Approx(100.0 * 42.0 / 29.7));  // ≈ 141.4
  CHECK(r.y == doctest::Approx(0.0));
  CHECK(r.x == doctest::Approx((200.0 - r.width) / 2.0));  // centered horizontally
}

TEST_CASE("centeredCrop of an already-correct-aspect image is the whole image") {
  // A blank image generated at the page size has the page aspect already, so the
  // default crop must not cut anything (no visible change on load).
  const double aspect = cropAspect(kA3W, kA3H, false);  // portrait
  const auto r = centeredCrop(1123, 1587, aspect);       // ≈ A3 @ 96dpi
  // The rounded blank size isn't EXACTLY √2, so at most a sub-pixel sliver is
  // trimmed — the crop must still cover essentially the whole image.
  CHECK(r.x < 1.0);
  CHECK(r.y < 1.0);
  CHECK(r.width == doctest::Approx(1123.0).epsilon(0.01));
  CHECK(r.height == doctest::Approx(1587.0).epsilon(0.01));
}

TEST_CASE("resizeCropFromCorner keeps the aspect ratio and anchors the opposite corner") {
  // Start with a 100x141.4 portrait crop at the image origin.
  const double aspect = cropAspect(kA3W, kA3H, false);
  CropRect cur{0, 0, 100, 100 / aspect};
  // Drag the bottom-right corner (2) outward; top-left (0,0) stays anchored.
  const auto r = resizeCropFromCorner(cur, 2, 80, 999, aspect, 1000, 1000);
  CHECK(r.x == doctest::Approx(0.0));
  CHECK(r.y == doctest::Approx(0.0));
  // Aspect preserved exactly.
  CHECK(r.width / r.height == doctest::Approx(aspect));
}

TEST_CASE("resizeCropFromCorner clamps to the image bounds") {
  const double aspect = cropAspect(kA3W, kA3H, true);  // album ≈ 1.414
  CropRect cur{10, 10, 100, 100 / aspect};
  // Drag bottom-right far past the image — must clamp inside 200x200.
  const auto r = resizeCropFromCorner(cur, 2, 5000, 5000, aspect, 200, 200);
  CHECK(r.x + r.width <= doctest::Approx(200.0));
  CHECK(r.y + r.height <= doctest::Approx(200.0));
  CHECK(r.width / r.height == doctest::Approx(aspect));
}

TEST_CASE("resizeCropFromCorner anchors a different corner correctly") {
  const double aspect = 1.0;  // square keeps the math obvious
  CropRect cur{100, 100, 100, 100};
  // Drag the top-left corner (0): bottom-right (200,200) is the anchor.
  const auto r = resizeCropFromCorner(cur, 0, 150, 150, aspect, 1000, 1000);
  CHECK(r.x + r.width == doctest::Approx(200.0));
  CHECK(r.y + r.height == doctest::Approx(200.0));
  CHECK(r.width == doctest::Approx(50.0));
  CHECK(r.height == doctest::Approx(50.0));
}

TEST_CASE("moveCropClamped keeps the rectangle inside the image") {
  CropRect cur{10, 10, 100, 80};
  CHECK(moveCropClamped(cur, 20, 30, 1000, 1000).x == doctest::Approx(30.0));
  // Push past the left/top edge → clamps to 0.
  CHECK(moveCropClamped(cur, -999, -999, 1000, 1000).x == doctest::Approx(0.0));
  CHECK(moveCropClamped(cur, -999, -999, 1000, 1000).y == doctest::Approx(0.0));
  // Push past the right edge → clamps to imageW - width.
  CHECK(moveCropClamped(cur, 9999, 0, 500, 500).x == doctest::Approx(400.0));
}

TEST_CASE("cropResizeScale is the width ratio, guarded against zero") {
  CHECK(cropResizeScale(100, 200) == doctest::Approx(2.0));
  CHECK(cropResizeScale(200, 100) == doctest::Approx(0.5));
  CHECK(cropResizeScale(0, 100) == doctest::Approx(1.0));
}

TEST_CASE("cropChange flags an orientation flip and otherwise reports the scale") {
  CropRect portrait{0, 0, 100, 141};
  CropRect biggerPortrait{0, 0, 200, 282};
  CropRect album{0, 0, 141, 100};

  const auto resized = cropChange(portrait, biggerPortrait);
  CHECK_FALSE(resized.orientationChanged);
  CHECK(resized.scale == doctest::Approx(2.0));

  const auto flipped = cropChange(portrait, album);
  CHECK(flipped.orientationChanged);
  CHECK(flipped.scale == doctest::Approx(1.0));  // points are cleared, not scaled
}

TEST_CASE("scaleLinePoints multiplies every point in place") {
  Lines lines;
  Line a;
  a.points = {{10, 20}, {30, 40}};
  Line b;
  b.points = {{1, 2}};
  lines = {a, b};
  scaleLinePoints(lines, 1.5);
  CHECK(lines[0].points[0].x == doctest::Approx(15.0));
  CHECK(lines[0].points[0].y == doctest::Approx(30.0));
  CHECK(lines[0].points[1].x == doctest::Approx(45.0));
  CHECK(lines[1].points[0].x == doctest::Approx(1.5));
  CHECK(lines[1].points[0].y == doctest::Approx(3.0));
}

TEST_CASE("rotateCropRectQuarter turns the crop into the swapped-dimension image") {
  const CropRect r{10, 20, 80, 40};  // in a 200x100 image
  const auto cw = rotateCropRectQuarter(r, 200, 100, true);
  CHECK(cw.x == doctest::Approx(100 - (20 + 40)));
  CHECK(cw.y == doctest::Approx(10));
  CHECK(cw.width == doctest::Approx(40));
  CHECK(cw.height == doctest::Approx(80));

  const auto ccw = rotateCropRectQuarter(r, 200, 100, false);
  CHECK(ccw.x == doctest::Approx(20));
  CHECK(ccw.y == doctest::Approx(200 - (10 + 80)));
  CHECK(ccw.width == doctest::Approx(40));
  CHECK(ccw.height == doctest::Approx(80));
}

TEST_CASE("rotateCropRectQuarter round-trips CW then CCW") {
  const CropRect r{13, 7, 50, 30};
  const auto cw = rotateCropRectQuarter(r, 200, 100, true);     // H x W space
  const auto back = rotateCropRectQuarter(cw, 100, 200, false); // back to W x H
  CHECK(back.x == doctest::Approx(r.x));
  CHECK(back.y == doctest::Approx(r.y));
  CHECK(back.width == doctest::Approx(r.width));
  CHECK(back.height == doctest::Approx(r.height));
}

TEST_CASE("rotateLinePointsQuarter turns crop-local points and round-trips") {
  Line a;
  a.points = {{0, 0}, {80, 0}, {80, 40}};
  Lines cw{a};
  rotateLinePointsQuarter(cw, 80, 40, true);  // box 80x40 -> 40x80
  CHECK(cw[0].points[0].x == doctest::Approx(40));
  CHECK(cw[0].points[0].y == doctest::Approx(0));
  CHECK(cw[0].points[1].x == doctest::Approx(40));
  CHECK(cw[0].points[1].y == doctest::Approx(80));
  CHECK(cw[0].points[2].x == doctest::Approx(0));
  CHECK(cw[0].points[2].y == doctest::Approx(80));

  Lines rt{a};
  rotateLinePointsQuarter(rt, 80, 40, true);
  rotateLinePointsQuarter(rt, 40, 80, false);  // swapped box restores
  for (std::size_t i = 0; i < a.points.size(); ++i) {
    CHECK(rt[0].points[i].x == doctest::Approx(a.points[i].x));
    CHECK(rt[0].points[i].y == doctest::Approx(a.points[i].y));
  }
}

TEST_CASE("scaleCropCentered grows/shrinks about the centre, keeps aspect, clamps") {
  const CropRect cur{60, 60, 80, 80};  // centre (100,100) in a 200x200 image
  SUBCASE("grow keeps centre + square aspect") {
    const CropRect r = scaleCropCentered(cur, 1.5, 1.0, 200, 200);
    CHECK(r.width == doctest::Approx(120));
    CHECK(r.height == doctest::Approx(120));
    CHECK(r.x + r.width / 2 == doctest::Approx(100));
    CHECK(r.y + r.height / 2 == doctest::Approx(100));
  }
  SUBCASE("over-grow is capped by the nearer edge and stays in bounds") {
    const CropRect r = scaleCropCentered(cur, 100.0, 1.0, 200, 200);
    CHECK(r.width == doctest::Approx(200));
    CHECK(r.x >= -1e-9);
    CHECK(r.x + r.width <= 200 + 1e-9);
  }
  SUBCASE("shrink floors at minSize") {
    const CropRect r = scaleCropCentered(cur, 0.0001, 1.0, 200, 200);
    CHECK(r.width == doctest::Approx(16));
    CHECK(r.height == doctest::Approx(16));
  }
}

#include "doctest.h"
#include "core/cropGeometry.hpp"

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

#include "core/zoomPan.hpp"
#include "doctest.h"

using namespace stencil::core;

TEST_CASE("clampScale matches zoomPan.js limits [0.05, 5]") {
  CHECK(clampScale(0.0) == doctest::Approx(0.05));
  CHECK(clampScale(10.0) == doctest::Approx(5.0));
  CHECK(clampScale(1.0) == doctest::Approx(1.0));
}

TEST_CASE("anchoredZoom keeps the focal pixel under the cursor fixed") {
  // Cursor at viewport (100, 50), scrolled to (200, 100), zoom 1 -> 2.
  // The image pixel under the cursor is contentX/oldScale = (100+200)/1 = 300.
  const auto z = anchoredZoom(200, 100, 100, 50, 1.0, 2.0);
  CHECK(z.scale == doctest::Approx(2.0));
  // After zoom the same pixel must still sit under the cursor:
  //   (scrollLeft' + cursorX) / newScale == imgX
  const double imgXBefore = (100 + 200) / 1.0;
  const double imgXAfter = (z.scrollLeft + 100) / z.scale;
  CHECK(imgXAfter == doctest::Approx(imgXBefore));
  const double imgYBefore = (50 + 100) / 1.0;
  const double imgYAfter = (z.scrollTop + 50) / z.scale;
  CHECK(imgYAfter == doctest::Approx(imgYBefore));
}

TEST_CASE("rectZoom fills + centers the swept rect (capped at 5x)") {
  // Rect 100x100 at (50,50); viewport 400x400 -> scale min(4,4,5)=4.
  const auto r = rectZoom(50, 50, 100, 100, 400, 400);
  CHECK(r.scale == doctest::Approx(4.0));
  // scrollLeft = max(0, 50*4 - (400 - 100*4)/2) = max(0, 200 - 0) = 200.
  CHECK(r.scrollLeft == doctest::Approx(200.0));
  CHECK(r.scrollTop == doctest::Approx(200.0));

  // Tiny rect would exceed 5x: cap applies.
  const auto capped = rectZoom(0, 0, 10, 10, 400, 400);
  CHECK(capped.scale == doctest::Approx(5.0));
}

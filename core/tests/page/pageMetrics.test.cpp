#include "doctest.h"
#include "pageMetrics.hpp"

using namespace stencil::core;

TEST_CASE("named page sizes match the constants table") {
  CHECK(namedPageSize("A4").width == doctest::Approx(21.0));
  CHECK(namedPageSize("A4").height == doctest::Approx(29.7));
  CHECK(namedPageSize("A3").width == doctest::Approx(29.7));
  CHECK(namedPageSize("A3").height == doctest::Approx(42.0));
}

TEST_CASE("named page sizes cover the full ISO A/B/C series") {
  CHECK(namedPageSize("A0").width == doctest::Approx(84.1));
  CHECK(namedPageSize("A0").height == doctest::Approx(118.9));
  CHECK(namedPageSize("A10").width == doctest::Approx(2.6));
  CHECK(namedPageSize("A10").height == doctest::Approx(3.7));
  CHECK(namedPageSize("B5").width == doctest::Approx(17.6));
  CHECK(namedPageSize("B5").height == doctest::Approx(25.0));
  CHECK(namedPageSize("C5").width == doctest::Approx(16.2));
  CHECK(namedPageSize("C5").height == doctest::Approx(22.9));
  CHECK(namedPageSize("C10").width == doctest::Approx(2.8));
  CHECK(namedPageSize("C10").height == doctest::Approx(4.0));
  // Exact case-sensitive match; unknown names stay {0,0}.
  CHECK(namedPageSize("b5").width == doctest::Approx(0.0));
  CHECK(namedPageSize("A11").width == doctest::Approx(0.0));
  CHECK(namedPageSize("custom").width == doctest::Approx(0.0));
}

TEST_CASE("pageFormatNames lists every format in canonical series order") {
  const std::string names = pageFormatNames();
  CHECK(names ==
        "A0 A1 A2 A3 A4 A5 A6 A7 A8 A9 A10 "
        "B0 B1 B2 B3 B4 B5 B6 B7 B8 B9 B10 "
        "C0 C1 C2 C3 C4 C5 C6 C7 C8 C9 C10");
}

TEST_CASE("portrait image keeps portrait page dimensions") {
  const auto d = pageDimensions("A4", 800, 1000, 0, 0);
  CHECK(d.width == doctest::Approx(21.0));
  CHECK(d.height == doctest::Approx(29.7));
}

TEST_CASE("landscape image swaps page dimensions") {
  const auto d = pageDimensions("A4", 1000, 800, 0, 0);
  CHECK(d.width == doctest::Approx(29.7));
  CHECK(d.height == doctest::Approx(21.0));
}

TEST_CASE("custom page size passes through unchanged") {
  const auto d = pageDimensions("custom", 1000, 800, 15.0, 25.0);
  CHECK(d.width == doctest::Approx(15.0));
  CHECK(d.height == doctest::Approx(25.0));
}

TEST_CASE("raw pixel -> page conversion scales linearly") {
  PageSize dims{21.0, 29.7};
  // Center of a 210x297 px image maps to the center of the page in cm.
  const auto p = pixelToPageRaw(105, 148.5, dims, 210, 297);
  CHECK(p.x == doctest::Approx(10.5));
  CHECK(p.y == doctest::Approx(14.85));
}

TEST_CASE("raw conversion is safe on a zero-sized canvas") {
  PageSize dims{21.0, 29.7};
  const auto p = pixelToPageRaw(5, 5, dims, 0, 0);
  CHECK(p.x == doctest::Approx(0.0));
  CHECK(p.y == doctest::Approx(0.0));
}

TEST_CASE("default blank-image size renders the page at 96 dpi") {
  const auto a4 = defaultBlankSizePx(namedPageSize("A4"));
  CHECK(a4.width == 794);    // round(21 / 2.54 * 96)
  CHECK(a4.height == 1123);  // round(29.7 / 2.54 * 96)
  const auto a3 = defaultBlankSizePx(namedPageSize("A3"));
  CHECK(a3.width == 1123);
  CHECK(a3.height == 1587);  // round(42 / 2.54 * 96)
  const auto b5 = defaultBlankSizePx(namedPageSize("B5"));
  CHECK(b5.width == 665);    // round(17.6 / 2.54 * 96)
  CHECK(b5.height == 945);   // round(25 / 2.54 * 96)
}

TEST_CASE("default blank-image size honors a custom dpi") {
  const auto px = defaultBlankSizePx({2.54, 5.08}, 100.0);
  CHECK(px.width == 100);
  CHECK(px.height == 200);
}

TEST_CASE("default blank-image size never collapses below 1px") {
  const auto px = defaultBlankSizePx({0.0, 0.001});
  CHECK(px.width == 1);
  CHECK(px.height == 1);
}

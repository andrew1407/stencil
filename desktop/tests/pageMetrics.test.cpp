#include "doctest.h"
#include "core/pageMetrics.hpp"

using namespace stencil::core;

TEST_CASE("named page sizes match the constants table") {
  CHECK(namedPageSize("A4").width == doctest::Approx(21.0));
  CHECK(namedPageSize("A4").height == doctest::Approx(29.7));
  CHECK(namedPageSize("A3").width == doctest::Approx(29.7));
  CHECK(namedPageSize("A3").height == doctest::Approx(42.0));
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

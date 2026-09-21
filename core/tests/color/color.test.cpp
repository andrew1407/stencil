#include "doctest.h"
#include "color.hpp"

using namespace stencil::core;

// Mirrors browser/tests/color.test.js.

TEST_CASE("#FF0000 with alpha 0.5 -> rgba(255,0,0,0.5)") {
  CHECK(hexToRgba("#FF0000", 0.5) == "rgba(255,0,0,0.5)");
}

TEST_CASE("lowercase #00ff00 with alpha 1 -> rgba(0,255,0,1)") {
  CHECK(hexToRgba("#00ff00", 1) == "rgba(0,255,0,1)");
}

TEST_CASE("pass-through for named color \"transparent\"") {
  CHECK(hexToRgba("transparent", 0.5) == "transparent");
}

TEST_CASE("pass-through for existing rgba string") {
  CHECK(hexToRgba("rgba(1,2,3,0.4)", 0.5) == "rgba(1,2,3,0.4)");
}

TEST_CASE("pass-through for short hex") {
  CHECK(hexToRgba("#fff", 0.5) == "#fff");
}

TEST_CASE("parseHex(#3399ff) -> {51,153,255}") {
  const auto rgb = parseHex("#3399ff");
  REQUIRE(rgb.has_value());
  CHECK(rgb->r == 51);
  CHECK(rgb->g == 153);
  CHECK(rgb->b == 255);
}

TEST_CASE("parseHex rejects non-hex / short input") {
  CHECK_FALSE(parseHex("#fff").has_value());
  CHECK_FALSE(parseHex("transparent").has_value());
  CHECK_FALSE(parseHex("#gggggg").has_value());
}

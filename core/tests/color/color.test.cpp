#include "doctest.h"
#include "color.hpp"

using namespace stencil::core;

// Mirrors the parseHex cases of browser/tests/utils/color.test.js.

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

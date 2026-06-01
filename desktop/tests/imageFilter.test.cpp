#include "core/imageFilter.hpp"
#include "doctest.h"
#include <cstdint>
#include <vector>

using namespace stencil::core;

TEST_SUITE("imageFilter") {
  TEST_CASE("filterModeFromString maps the browser strings") {
    CHECK(filterModeFromString("none") == FilterMode::None);
    CHECK(filterModeFromString("") == FilterMode::None);
    CHECK(filterModeFromString("bw") == FilterMode::Bw);
    CHECK(filterModeFromString("sepia") == FilterMode::Sepia);
    CHECK(filterModeFromString("custom") == FilterMode::Custom);
    // Any other non-"none" value is the custom tint (renderer.js else branch).
    CHECK(filterModeFromString("teal") == FilterMode::Custom);
  }

  TEST_CASE("filterPixel None returns the source unchanged") {
    const Rgb8 o = filterPixel(FilterMode::None, 12, 34, 56, 1, 2, 3);
    CHECK(o.r == 12);
    CHECK(o.g == 34);
    CHECK(o.b == 56);
  }

  TEST_CASE("filterPixel Bw uses Rec. 709 luma (truncated)") {
    // Pure red: l = (int)(0.2126 * 200) = (int)42.52 = 42 on all channels.
    const Rgb8 o = filterPixel(FilterMode::Bw, 200, 0, 0, 0, 0, 0);
    CHECK(o.r == 42);
    CHECK(o.g == 42);
    CHECK(o.b == 42);
    // Black stays black.
    const Rgb8 black = filterPixel(FilterMode::Bw, 0, 0, 0, 0, 0, 0);
    CHECK(black.r == 0);
    CHECK(black.g == 0);
    CHECK(black.b == 0);
  }

  TEST_CASE("filterPixel Sepia matches the CSS matrix, clamped high") {
    // White: red/green channels saturate to 255; blue = (int)(255*0.937) = 238.
    const Rgb8 white = filterPixel(FilterMode::Sepia, 255, 255, 255, 0, 0, 0);
    CHECK(white.r == 255);
    CHECK(white.g == 255);
    CHECK(white.b == 238);
    const Rgb8 black = filterPixel(FilterMode::Sepia, 0, 0, 0, 0, 0, 0);
    CHECK(black.r == 0);
    CHECK(black.g == 0);
    CHECK(black.b == 0);
  }

  TEST_CASE("filterPixel Custom maps a dark pixel onto the tint color") {
    // Black (luma 0, t=0) -> exactly the tint color, for any tint.
    const Rgb8 o = filterPixel(FilterMode::Custom, 0, 0, 0, 124, 58, 237);
    CHECK(o.r == 124);
    CHECK(o.g == 58);
    CHECK(o.b == 237);
  }

  TEST_CASE("filterPixel Custom with a black tint degenerates to grayscale") {
    // tint (0,0,0): r' = lround((255-0) * l/255) = lround(l). So a pure-red
    // pixel (luma 42) maps to {42,42,42}, matching the Bw branch.
    const Rgb8 o = filterPixel(FilterMode::Custom, 200, 0, 0, 0, 0, 0);
    CHECK(o.r == 42);
    CHECK(o.g == 42);
    CHECK(o.b == 42);
  }

  TEST_CASE("applyFilterRGBA preserves alpha and is a no-op for None") {
    // Two pixels: red (a=10), green (a=20).
    std::vector<std::uint8_t> buf = {200, 0, 0, 10, 0, 200, 0, 20};

    SUBCASE("None leaves the buffer untouched") {
      const std::vector<std::uint8_t> before = buf;
      applyFilterRGBA(FilterMode::None, buf.data(), 2, 9, 9, 9);
      CHECK(buf == before);
    }

    SUBCASE("Bw grayscales both pixels but keeps each alpha") {
      applyFilterRGBA(FilterMode::Bw, buf.data(), 2, 0, 0, 0);
      // red 200 -> luma 42
      CHECK(buf[0] == 42);
      CHECK(buf[1] == 42);
      CHECK(buf[2] == 42);
      CHECK(buf[3] == 10);  // alpha preserved
      // green 200 -> luma (int)(0.7152*200)=143
      CHECK(buf[4] == 143);
      CHECK(buf[5] == 143);
      CHECK(buf[6] == 143);
      CHECK(buf[7] == 20);  // alpha preserved
    }
  }

  TEST_CASE("applyFilterRGBA Custom tints dark pixels and keeps alpha") {
    std::vector<std::uint8_t> buf = {0, 0, 0, 200};  // black, a=200
    applyFilterRGBA(FilterMode::Custom, buf.data(), 1, 124, 58, 237);
    CHECK(buf[0] == 124);
    CHECK(buf[1] == 58);
    CHECK(buf[2] == 237);
    CHECK(buf[3] == 200);  // alpha untouched
  }

  TEST_CASE("applyFilterRGBA tolerates a null buffer / zero count") {
    applyFilterRGBA(FilterMode::Bw, nullptr, 0, 0, 0, 0);  // must not crash
    std::vector<std::uint8_t> buf = {1, 2, 3, 4};
    applyFilterRGBA(FilterMode::Bw, buf.data(), 0, 0, 0, 0);
    CHECK(buf[0] == 1);  // zero count -> nothing written
  }
}

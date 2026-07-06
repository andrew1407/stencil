#include "imageFilter.hpp"
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
    CHECK(filterModeFromString("invert") == FilterMode::Invert);
    CHECK(filterModeFromString("contour") == FilterMode::Contour);
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

  TEST_CASE("filterPixel Invert flips every channel") {
    const Rgb8 o = filterPixel(FilterMode::Invert, 12, 34, 56, 9, 9, 9);
    CHECK(o.r == 243);  // 255 - 12
    CHECK(o.g == 221);  // 255 - 34
    CHECK(o.b == 199);  // 255 - 56
    // Black <-> white round trip.
    const Rgb8 white = filterPixel(FilterMode::Invert, 0, 0, 0, 0, 0, 0);
    CHECK(white.r == 255);
    CHECK(white.g == 255);
    CHECK(white.b == 255);
  }

  TEST_CASE("filterPixel Contour passes the source through (not per-pixel)") {
    const Rgb8 o = filterPixel(FilterMode::Contour, 12, 34, 56, 9, 9, 9);
    CHECK(o.r == 12);
    CHECK(o.g == 34);
    CHECK(o.b == 56);
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
      CHECK(buf[3] == 10);
      // green 200 -> luma (int)(0.7152*200)=143
      CHECK(buf[4] == 143);
      CHECK(buf[5] == 143);
      CHECK(buf[6] == 143);
      CHECK(buf[7] == 20);
    }
  }

  TEST_CASE("applyFilterRGBA Custom tints dark pixels and keeps alpha") {
    std::vector<std::uint8_t> buf = {0, 0, 0, 200};  // black, a=200
    applyFilterRGBA(FilterMode::Custom, buf.data(), 1, 124, 58, 237);
    CHECK(buf[0] == 124);
    CHECK(buf[1] == 58);
    CHECK(buf[2] == 237);
    CHECK(buf[3] == 200);
  }

  TEST_CASE("applyFilterRGBA Invert flips both pixels and keeps alpha") {
    std::vector<std::uint8_t> buf = {12, 34, 56, 10, 255, 0, 128, 20};
    applyFilterRGBA(FilterMode::Invert, buf.data(), 2, 9, 9, 9);
    CHECK(buf[0] == 243);
    CHECK(buf[1] == 221);
    CHECK(buf[2] == 199);
    CHECK(buf[3] == 10);
    CHECK(buf[4] == 0);
    CHECK(buf[5] == 255);
    CHECK(buf[6] == 127);
    CHECK(buf[7] == 20);
  }

  TEST_CASE("applyFilterRGBA Contour is a no-op (needs dimensions)") {
    std::vector<std::uint8_t> buf = {12, 34, 56, 10};
    const std::vector<std::uint8_t> before = buf;
    applyFilterRGBA(FilterMode::Contour, buf.data(), 1, 9, 9, 9);
    CHECK(buf == before);  // callers with dimensions use applyContourRGBA
  }

  TEST_CASE("applyFilterRGBA tolerates a null buffer / zero count") {
    applyFilterRGBA(FilterMode::Bw, nullptr, 0, 0, 0, 0);  // must not crash
    std::vector<std::uint8_t> buf = {1, 2, 3, 4};
    applyFilterRGBA(FilterMode::Bw, buf.data(), 0, 0, 0, 0);
    CHECK(buf[0] == 1);  // zero count -> nothing written
  }

  TEST_CASE("applyContourRGBA maps a uniform image to all white, keeping alpha") {
    // 3x2 of one color: every Sobel gradient is 0, so mag 0 -> 255 everywhere.
    std::vector<std::uint8_t> buf;
    for (int i = 0; i < 6; ++i) {
      buf.push_back(100);
      buf.push_back(150);
      buf.push_back(200);
      buf.push_back(static_cast<std::uint8_t>(40 + i));  // distinct alphas
    }
    applyContourRGBA(buf.data(), 3, 2);
    for (int i = 0; i < 6; ++i) {
      CHECK(buf[i * 4 + 0] == 255);
      CHECK(buf[i * 4 + 1] == 255);
      CHECK(buf[i * 4 + 2] == 255);
      CHECK(buf[i * 4 + 3] == 40 + i);
    }
  }

  TEST_CASE("applyContourRGBA marks a hard vertical edge (hand-computed Sobel)") {
    // 4x1: black, black, white, white. Luma L = [0, 0, 255, 255]; with the row
    // clamped vertically, gy = 0 and gx = 4 * (l(x+1) - l(x-1)):
    //   x=0: 4*(  0 -   0) =    0 -> 255 - 0   = 255
    //   x=1: 4*(255 -   0) = 1020 -> mag clamps to 255 -> 0
    //   x=2: 4*(255 -   0) = 1020 -> 0
    //   x=3: 4*(255 - 255) =    0 -> 255  (x+1 clamps to the last column)
    std::vector<std::uint8_t> buf = {0,   0,   0,   10,  0,   0,   0,   20,
                                     255, 255, 255, 30,  255, 255, 255, 40};
    applyContourRGBA(buf.data(), 4, 1);
    const std::vector<std::uint8_t> expected = {255, 255, 255, 10,  0, 0, 0, 20,
                                                0,   0,   0,   30,  255, 255,
                                                255, 40};
    CHECK(buf == expected);
  }

  TEST_CASE("applyContourRGBA computes exact non-saturating magnitudes") {
    // 3x1 gray ramp 0, 10, 20: L equals the gray value, gy = 0,
    // gx = 4 * (l(x+1) - l(x-1)) with clamped columns:
    //   x=0: 4*(10 -  0) = 40 -> 215
    //   x=1: 4*(20 -  0) = 80 -> 175
    //   x=2: 4*(20 - 10) = 40 -> 215
    std::vector<std::uint8_t> buf = {0,  0,  0,  1,  10, 10, 10, 2,
                                     20, 20, 20, 3};
    applyContourRGBA(buf.data(), 3, 1);
    const std::vector<std::uint8_t> expected = {215, 215, 215, 1,  175, 175,
                                                175, 2,   215, 215, 215, 3};
    CHECK(buf == expected);
  }

  TEST_CASE("applyContourRGBA tolerates a null buffer / degenerate dimensions") {
    applyContourRGBA(nullptr, 3, 3);  // must not crash
    std::vector<std::uint8_t> buf = {1, 2, 3, 4};
    applyContourRGBA(buf.data(), 0, 1);
    applyContourRGBA(buf.data(), 1, -1);
    CHECK(buf[0] == 1);  // degenerate dims -> nothing written
  }

  TEST_CASE("applyContourRGBA turns a 1x1 image white") {
    // All clamped neighbors are the pixel itself, so gx = gy = 0 -> 255.
    std::vector<std::uint8_t> buf = {5, 200, 30, 77};
    applyContourRGBA(buf.data(), 1, 1);
    CHECK(buf[0] == 255);
    CHECK(buf[1] == 255);
    CHECK(buf[2] == 255);
    CHECK(buf[3] == 77);  // alpha preserved
  }
}

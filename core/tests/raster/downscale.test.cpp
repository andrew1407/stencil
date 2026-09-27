#include "doctest.h"
#include "cliApi.h"
#include "downscale.hpp"

#include <cstdint>
#include <vector>

using namespace stencil::core;

namespace {
  using Pixels = std::vector<std::uint8_t>;

  Pixels solid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    Pixels v;
    for (int i = 0; i < w * h; ++i) v.insert(v.end(), {r, g, b, a});
    return v;
  }

  Pixels shrink(const Pixels& src, int w, int h, int dw, int dh) {
    Pixels dst(static_cast<std::size_t>(dw) * dh * 4, 7);
    CHECK(downscaleRGBA(src.data(), w, h, dst.data(), dw, dh));
    return dst;
  }

  Pixels px(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) { return {r, g, b, a}; }
}  // namespace

TEST_CASE("thumbnailDims fits the longer side, keeps the aspect and never upscales") {
  int w = 0, h = 0;
  thumbnailDims(4000, 3000, 512, w, h); CHECK((w == 512 && h == 384));
  thumbnailDims(3000, 4000, 512, w, h); CHECK((w == 384 && h == 512));
  thumbnailDims(1000, 1000, 10, w, h); CHECK((w == 10 && h == 10));
  thumbnailDims(1001, 1000, 10, w, h); CHECK((w == 10 && h == 10));  // 9.99 rounds
  thumbnailDims(10000, 1, 100, w, h); CHECK((w == 100 && h == 1));   // never below 1
  thumbnailDims(300, 200, 512, w, h); CHECK((w == 300 && h == 200)); // already fits
  thumbnailDims(512, 100, 512, w, h); CHECK((w == 512 && h == 100));
  thumbnailDims(300, 200, 0, w, h); CHECK((w == 300 && h == 200));
  thumbnailDims(0, 5, 2, w, h); CHECK((w == 0 && h == 5));
}

TEST_CASE("downscaleRGBA keeps a solid colour exact at integer and fractional ratios") {
  const Pixels opaque = solid(7, 5, 12, 34, 56, 255);
  CHECK(shrink(opaque, 7, 5, 3, 2) == solid(3, 2, 12, 34, 56, 255));
  CHECK(shrink(opaque, 7, 5, 1, 1) == solid(1, 1, 12, 34, 56, 255));
  const Pixels half = solid(8, 6, 200, 100, 50, 128);
  CHECK(shrink(half, 8, 6, 4, 3) == solid(4, 3, 200, 100, 50, 128));
  CHECK(shrink(half, 8, 6, 5, 4) == solid(5, 4, 200, 100, 50, 128));
}

TEST_CASE("downscaleRGBA 2:1 is the hand-computed box mean, rounded half up") {
  // 4x2 -> 2x1: the left 2x2 block and the right 2x2 block, all opaque.
  Pixels src(4 * 2 * 4, 255);
  const std::uint8_t reds[8] = {10, 20, 0, 1, 30, 40, 0, 1};  // row-major 4x2
  for (int i = 0; i < 8; ++i) src[static_cast<std::size_t>(i) * 4] = reds[i];
  const Pixels out = shrink(src, 4, 2, 2, 1);
  CHECK(out[0] == 25);  // (10 + 20 + 30 + 40) / 4
  CHECK(out[4] == 1);   // (0 + 1 + 0 + 1) / 4 = 0.5 rounds up
  CHECK((out[3] == 255 && out[7] == 255));
}

TEST_CASE("downscaleRGBA weighs a straddling pixel by the area it covers") {
  // 3 -> 2: out0 = (2*0 + 1*90) / 3, out1 = (1*90 + 2*180) / 3.
  Pixels src;
  for (std::uint8_t v : {0, 90, 180}) src.insert(src.end(), {v, v, v, 255});
  const Pixels out = shrink(src, 3, 1, 2, 1);
  CHECK(out == Pixels{30, 30, 30, 255, 150, 150, 150, 255});
}

TEST_CASE("downscaleRGBA at an integer ratio is each block's exact mean") {
  Pixels src(12 * 8 * 4, 255);
  for (std::size_t i = 0; i < 12 * 8; ++i) {
    src[i * 4] = static_cast<std::uint8_t>(i * 7 % 256);
    src[i * 4 + 1] = static_cast<std::uint8_t>(i * 13 % 256);
  }
  const Pixels out = shrink(src, 12, 8, 4, 2);  // 3x4 blocks
  for (int oy = 0; oy < 2; ++oy) {
    for (int ox = 0; ox < 4; ++ox) {
      for (int c = 0; c < 2; ++c) {
        int sum = 0;
        for (int y = oy * 4; y < oy * 4 + 4; ++y) {
          for (int x = ox * 3; x < ox * 3 + 3; ++x) sum += src[static_cast<std::size_t>((y * 12 + x) * 4 + c)];
        }
        CHECK(out[static_cast<std::size_t>((oy * 4 + ox) * 4 + c)] == (sum + 6) / 12);
      }
    }
  }
}

TEST_CASE("downscaleRGBA averages premultiplied, so a transparent neighbour adds no dark") {
  // White beside transparent black: a naive mean would be (128,128,128,128).
  Pixels edge = px(255, 255, 255, 255);
  edge.insert(edge.end(), {0, 0, 0, 0});
  CHECK(shrink(edge, 2, 1, 1, 1) == px(255, 255, 255, 128));
  // Opaque red beside half-alpha blue: colour weighted by alpha, (65025+191)/383, (32640+191)/383.
  Pixels mixed = px(255, 0, 0, 255);
  mixed.insert(mixed.end(), {0, 0, 255, 128});
  CHECK(shrink(mixed, 2, 1, 1, 1) == px(170, 0, 85, 192));
  CHECK(shrink(solid(4, 4, 90, 90, 90, 0), 4, 4, 2, 2) == solid(2, 2, 0, 0, 0, 0));
}

TEST_CASE("downscaleRGBA copies an equal size verbatim and refuses to upscale") {
  const Pixels clear = solid(3, 2, 9, 8, 7, 0);
  CHECK(shrink(clear, 3, 2, 3, 2) == clear);  // a transparent pixel keeps its colour
  Pixels dst(4 * 4 * 4, 7);
  CHECK_FALSE(downscaleRGBA(clear.data(), 3, 2, dst.data(), 4, 2));
  CHECK_FALSE(downscaleRGBA(clear.data(), 3, 2, dst.data(), 3, 3));
  CHECK_FALSE(downscaleRGBA(clear.data(), 3, 2, dst.data(), 0, 1));
  CHECK_FALSE(downscaleRGBA(nullptr, 3, 2, dst.data(), 1, 1));
  CHECK_FALSE(downscaleRGBA(clear.data(), 3, 2, nullptr, 1, 1));
  CHECK(dst == Pixels(4 * 4 * 4, 7));
}

TEST_CASE("stencil_cli_thumbnailDims / downscaleRGBA reach the same core") {
  int w = 0, h = 0;
  stencil_cli_thumbnailDims(4000, 3000, 512, &w, &h);
  CHECK((w == 512 && h == 384));
  stencil_cli_thumbnailDims(4000, 3000, 512, nullptr, nullptr);  // NULL out-pointers tolerated
  const Pixels src = solid(6, 4, 1, 2, 3, 255);
  Pixels dst(3 * 2 * 4, 0);
  CHECK(stencil_cli_downscaleRGBA(src.data(), 6, 4, dst.data(), 3, 2) == 1);
  CHECK(dst == solid(3, 2, 1, 2, 3, 255));
  CHECK(stencil_cli_downscaleRGBA(src.data(), 6, 4, dst.data(), 7, 2) == 0);
}

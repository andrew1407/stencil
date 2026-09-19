// Tiling proof for the row-range entry points: running a range function over a set of
// tiles must produce the SAME BYTES as the whole-image call, at every split point.
// This is the seam detector for the Sobel pass, whose 3x3 neighbourhood reads one row
// outside each tile.
#include "doctest.h"

#include "imageFilter.hpp"
#include "imageOps.hpp"
#include "rasterize.hpp"

#include <cstdint>
#include <vector>

using namespace stencil::core;

namespace {

  // Deterministic non-flat RGBA8 noise, so filters and Sobel do real work.
  std::vector<std::uint8_t> noise(int w, int h, std::uint32_t seed) {
    std::vector<std::uint8_t> b(static_cast<std::size_t>(w) * h * 4);
    std::uint32_t s = seed | 1u;
    for (auto& v : b) {
      s ^= s << 13; s ^= s >> 17; s ^= s << 5;
      v = static_cast<std::uint8_t>(s >> 7);
    }
    return b;
  }

  // Row splits: one tile, per-row tiles, uneven tiles that do not divide h. `overWide` adds a
  // range past the far edge - only for ops told the height (applyFilterRows is not, so y1 is real).
  std::vector<std::vector<std::pair<int, int>>> splits(int h, bool overWide = true) {
    std::vector<std::vector<std::pair<int, int>>> out;
    out.push_back({{0, h}});
    for (int tile : {1, 2, 3, 5, 7, 16}) {
      std::vector<std::pair<int, int>> tiles;
      for (int y = 0; y < h; y += tile) tiles.push_back({y, std::min(y + tile, h)});
      out.push_back(tiles);
    }
    // Ragged, out-of-order tiles that still cover [0, h) exactly once.
    if (h >= 4) out.push_back({{h / 2, h}, {0, 1}, {1, h / 2}});
    out.push_back({{-5, overWide ? h + 5 : h}});  // a negative start always clamps
    return out;
  }

  const int DIMS[][2] = {{1, 1}, {1, 9}, {9, 1}, {2, 2}, {5, 4}, {17, 13}, {64, 48}, {33, 31}};

}  // namespace

TEST_SUITE("rowRanges") {

  TEST_CASE("applyFilterRows tiled == applyFilterRGBA whole") {
    for (const auto& d : DIMS) {
      const int w = d[0], h = d[1];
      const auto src = noise(w, h, 11u * w + h);
      for (int m = 0; m <= 5; ++m) {
        const auto mode = static_cast<FilterMode>(m);
        std::vector<std::uint8_t> whole = src;
        applyFilterRGBA(mode, whole.data(), static_cast<std::size_t>(w) * h, 40, 90, 200);
        for (const auto& tiles : splits(h, /*overWide=*/false)) {
          std::vector<std::uint8_t> tiled = src;
          for (const auto& t : tiles)
            applyFilterRows(mode, tiled.data(), w, t.first, t.second, 40, 90, 200);
          CHECK(tiled == whole);
        }
      }
    }
  }

  TEST_CASE("applyFilterRows touches only its own rows") {
    const int w = 8, h = 6;
    const auto src = noise(w, h, 99u);
    std::vector<std::uint8_t> buf = src;
    applyFilterRows(FilterMode::INVERT, buf.data(), w, 2, 4, 0, 0, 0);
    for (int y = 0; y < h; ++y) {
      const std::size_t off = static_cast<std::size_t>(y) * w * 4;
      const bool inRange = y >= 2 && y < 4;
      for (int i = 0; i < w * 4; ++i) {
        const bool alpha = (i % 4) == 3;
        const std::uint8_t expect =
            (inRange && !alpha) ? static_cast<std::uint8_t>(255 - src[off + i]) : src[off + i];
        CHECK(buf[off + i] == expect);
      }
    }
    // An empty or inverted range is a no-op.
    std::vector<std::uint8_t> same = src;
    applyFilterRows(FilterMode::INVERT, same.data(), w, 3, 3, 0, 0, 0);
    applyFilterRows(FilterMode::INVERT, same.data(), w, 5, 2, 0, 0, 0);
    CHECK(same == src);
  }

  TEST_CASE("contour: two-phase tiled == applyContourRGBA whole (seam check)") {
    for (const auto& d : DIMS) {
      const int w = d[0], h = d[1];
      const auto src = noise(w, h, 7u * w + 3u * h);
      std::vector<std::uint8_t> whole = src;
      applyContourRGBA(whole.data(), w, h);
      for (const auto& tiles : splits(h)) {
        std::vector<std::uint8_t> tiled = src;
        std::vector<std::uint8_t> luma(static_cast<std::size_t>(w) * h, 0);
        for (const auto& t : tiles) buildLumaRows(tiled.data(), w, h, t.first, t.second, luma.data());
        for (const auto& t : tiles) sobelRows(luma.data(), tiled.data(), w, h, t.first, t.second);
        CHECK(tiled == whole);
      }
    }
  }

  TEST_CASE("contour: caller-supplied scratch matches, and survives reuse") {
    std::vector<std::uint8_t> scratch;
    for (const auto& d : DIMS) {
      const int w = d[0], h = d[1];
      const auto src = noise(w, h, 5u * w + h);
      std::vector<std::uint8_t> whole = src;
      applyContourRGBA(whole.data(), w, h);
      std::vector<std::uint8_t> reused = src;
      applyContourRGBA(reused.data(), w, h, scratch);  // scratch carries the last image's plane
      CHECK(reused == whole);
      CHECK(scratch.size() == static_cast<std::size_t>(w) * h);
    }
  }

  TEST_CASE("cropImageRows tiled == cropImageRGBA whole") {
    const int sw = 37, sh = 29;
    const auto src = noise(sw, sh, 4242u);
    const int rects[][4] = {{0, 0, 37, 29}, {5, 7, 10, 10},  {-3, -4, 12, 9}, {30, 25, 20, 20},
                            {-50, -50, 10, 10}, {40, 40, 5, 5}, {-1, 0, 39, 31}, {36, 28, 1, 1}};
    for (const auto& r : rects) {
      const int rw = r[2], rh = r[3];
      std::vector<std::uint8_t> whole(static_cast<std::size_t>(rw) * rh * 4, 0xAB);
      cropImageRGBA(src.data(), sw, sh, r[0], r[1], rw, rh, whole.data());
      for (const auto& tiles : splits(rh)) {
        std::vector<std::uint8_t> tiled(static_cast<std::size_t>(rw) * rh * 4, 0xAB);
        for (const auto& t : tiles)
          cropImageRows(src.data(), sw, sh, r[0], r[1], rw, rh, tiled.data(), t.first, t.second);
        CHECK(tiled == whole);
      }
    }
  }

  TEST_CASE("rotateImageRows tiled == rotateImageRGBA whole") {
    for (const auto& d : DIMS) {
      const int w = d[0], h = d[1];
      const auto src = noise(w, h, 13u * w + h);
      for (int q = -3; q <= 5; ++q) {
        int ow = 0, oh = 0;
        rotatedDims(w, h, q, ow, oh);
        std::vector<std::uint8_t> whole(static_cast<std::size_t>(ow) * oh * 4, 0xCD);
        rotateImageRGBA(src.data(), w, h, q, whole.data());
        for (const auto& tiles : splits(oh)) {
          std::vector<std::uint8_t> tiled(static_cast<std::size_t>(ow) * oh * 4, 0xCD);
          for (const auto& t : tiles)
            rotateImageRows(src.data(), w, h, q, tiled.data(), t.first, t.second);
          CHECK(tiled == whole);
        }
      }
    }
  }

  TEST_CASE("fillPolygonRows tiled == the whole-image fill") {
    const int w = 60, h = 45;
    const std::vector<std::vector<Point>> polys = {
        {{5, 5}, {55, 8}, {40, 40}, {12, 35}},                  // convex-ish quad
        {{-10, -10}, {70, -5}, {65, 50}, {-5, 55}},             // covers the canvas
        {{10, 10}, {50, 10}, {10, 40}, {50, 40}},               // self-crossing (even-odd)
        {{0.5, 0.5}, {59.5, 0.5}, {59.5, 44.5}, {0.5, 44.5}},   // half-pixel edges
        {{100, 100}, {150, 100}, {150, 150}},                   // wholly off-canvas
        {{20, 20}, {21, 20}, {21, 21}, {20, 21}}};              // sub-pixel sliver
    const Rgba c{200, 30, 90, 160};
    for (const auto& pts : polys) {
      std::vector<std::uint8_t> whole(static_cast<std::size_t>(w) * h * 4, 0x20);
      fillPolygonRows(whole.data(), w, h, pts, c, 0, h);
      for (const auto& tiles : splits(h)) {
        std::vector<std::uint8_t> tiled(static_cast<std::size_t>(w) * h * 4, 0x20);
        for (const auto& t : tiles) fillPolygonRows(tiled.data(), w, h, pts, c, t.first, t.second);
        CHECK(tiled == whole);
      }
    }
  }

  TEST_CASE("row entry points shrug off degenerate arguments") {
    std::vector<std::uint8_t> buf(4 * 4 * 4, 0x11);
    const std::vector<std::uint8_t> before = buf;
    applyFilterRows(FilterMode::BW, nullptr, 4, 0, 4, 0, 0, 0);
    applyFilterRows(FilterMode::BW, buf.data(), 0, 0, 4, 0, 0, 0);
    applyFilterRows(FilterMode::NONE, buf.data(), 4, 0, 4, 0, 0, 0);
    applyFilterRows(FilterMode::CONTOUR, buf.data(), 4, 0, 4, 0, 0, 0);
    buildLumaRows(nullptr, 4, 4, 0, 4, buf.data());
    sobelRows(nullptr, buf.data(), 4, 4, 0, 4);
    sobelRows(buf.data(), nullptr, 4, 4, 0, 4);
    rotateImageRows(nullptr, 4, 4, 1, buf.data(), 0, 4);
    rotateImageRows(buf.data(), 0, 4, 1, buf.data(), 0, 4);
    cropImageRows(nullptr, 4, 4, 0, 0, 4, 4, buf.data(), 0, 4);
    cropImageRows(buf.data(), 4, 4, 0, 0, 0, 4, buf.data(), 0, 4);
    CHECK(buf == before);
  }

}  // TEST_SUITE("rowRanges")

#include "doctest.h"
#include "imageOps.hpp"

#include <cstdint>
#include <vector>

using namespace stencil::core;

namespace {
  // Build a w*h RGBA image where each pixel's R channel encodes its index, so a
  // moved pixel is identifiable.
  std::vector<std::uint8_t> ramp(int w, int h) {
    std::vector<std::uint8_t> v(static_cast<std::size_t>(w) * h * 4, 0);
    for (int i = 0; i < w * h; ++i) {
      v[i * 4 + 0] = static_cast<std::uint8_t>(i);
      v[i * 4 + 3] = 255;
    }
    return v;
  }
}  // namespace

TEST_CASE("normalizeQuarters wraps into 0..3") {
  CHECK(normalizeQuarters(0) == 0);
  CHECK(normalizeQuarters(-1) == 3);
  CHECK(normalizeQuarters(3) == 3);
  CHECK(normalizeQuarters(4) == 0);
  CHECK(normalizeQuarters(5) == 1);
}

TEST_CASE("rotatedDims swaps on odd turns") {
  int ow = 0, oh = 0;
  rotatedDims(4, 2, 0, ow, oh); CHECK((ow == 4 && oh == 2));
  rotatedDims(4, 2, 1, ow, oh); CHECK((ow == 2 && oh == 4));
  rotatedDims(4, 2, 2, ow, oh); CHECK((ow == 4 && oh == 2));
  rotatedDims(4, 2, 3, ow, oh); CHECK((ow == 2 && oh == 4));
}

TEST_CASE("cropImageRGBA copies a sub-rect and zero-pads out of bounds") {
  auto src = ramp(2, 2);  // indices 0,1 / 2,3
  std::vector<std::uint8_t> dst(1 * 1 * 4, 7);
  cropImageRGBA(src.data(), 2, 2, 1, 0, 1, 1, dst.data());  // pixel index 1
  CHECK(dst[0] == 1);
  CHECK(dst[3] == 255);

  std::vector<std::uint8_t> oob(1 * 1 * 4, 7);
  cropImageRGBA(src.data(), 2, 2, 5, 5, 1, 1, oob.data());  // outside
  CHECK(oob[0] == 0);
  CHECK(oob[3] == 0);
}

// A negative-origin crop that straddles the source edge: the overlapping region is
// copied, the negative/overflow rim is zero-padded. Every front-end feeds rx/ry from
// user drags, so this mixed in/out-of-bounds path must be exact.
TEST_CASE("cropImageRGBA zero-pads the rim of a negative-origin partial overlap") {
  auto src = ramp(2, 2);  // R: (0,0)=0 (1,0)=1 (0,1)=2 (1,1)=3, alpha 255
  std::vector<std::uint8_t> dst(3 * 3 * 4, 9);
  cropImageRGBA(src.data(), 2, 2, -1, -1, 3, 3, dst.data());
  auto R = [&](int dx, int dy) { return dst[(dy * 3 + dx) * 4 + 0]; };
  auto A = [&](int dx, int dy) { return dst[(dy * 3 + dx) * 4 + 3]; };
  // Top row and left column map to negative source coords -> fully transparent.
  for (int dx = 0; dx < 3; ++dx) { CHECK(R(dx, 0) == 0); CHECK(A(dx, 0) == 0); }
  for (int dy = 0; dy < 3; ++dy) { CHECK(R(0, dy) == 0); CHECK(A(0, dy) == 0); }
  // The overlapping bottom-right 2x2 carries the source pixels (alpha restored).
  CHECK(R(1, 1) == 0); CHECK(A(1, 1) == 255);  // src(0,0)
  CHECK(R(2, 1) == 1); CHECK(A(2, 1) == 255);  // src(1,0)
  CHECK(R(1, 2) == 2); CHECK(A(1, 2) == 255);  // src(0,1)
  CHECK(R(2, 2) == 3); CHECK(A(2, 2) == 255);  // src(1,1)
}

TEST_CASE("rotateImageRGBA 90 clockwise maps a 2x1 row to a column") {
  auto src = ramp(2, 1);  // [A=0][B=1]
  std::vector<std::uint8_t> dst(1 * 2 * 4, 0);
  rotateImageRGBA(src.data(), 2, 1, 1, dst.data());  // out dims 1x2
  CHECK(dst[0 * 4 + 0] == 0);  // top    = A
  CHECK(dst[1 * 4 + 0] == 1);  // bottom = B
}

TEST_CASE("rotateImageRGBA 180 reverses pixel order") {
  auto src = ramp(2, 2);  // R = 0,1 / 2,3
  std::vector<std::uint8_t> dst(2 * 2 * 4, 0);  // out dims 2x2
  rotateImageRGBA(src.data(), 2, 2, 2, dst.data());
  CHECK(dst[0 * 4 + 0] == 3);  // pixels run in reverse
  CHECK(dst[1 * 4 + 0] == 2);
  CHECK(dst[2 * 4 + 0] == 1);
  CHECK(dst[3 * 4 + 0] == 0);
}

TEST_CASE("rotateImageRGBA 270 clockwise is the inverse of 90") {
  auto src = ramp(2, 1);  // [A=0][B=1]
  std::vector<std::uint8_t> dst(1 * 2 * 4, 0);  // out dims 1x2
  rotateImageRGBA(src.data(), 2, 1, 3, dst.data());
  CHECK(dst[0 * 4 + 0] == 1);  // top    = B  (opposite of the 90° case)
  CHECK(dst[1 * 4 + 0] == 0);  // bottom = A
}

TEST_CASE("fillRGBA writes a solid colour") {
  std::vector<std::uint8_t> buf(2 * 4, 0);
  fillRGBA(buf.data(), 2, 10, 20, 30, 40);
  CHECK(buf[0] == 10); CHECK(buf[1] == 20); CHECK(buf[2] == 30); CHECK(buf[3] == 40);
  CHECK(buf[4] == 10); CHECK(buf[7] == 40);
}

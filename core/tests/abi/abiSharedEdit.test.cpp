// The crop and merge exports abi/shared.inc emits into both extern "C" ABIs: each case calls
// the wasm and the CLI spelling and asserts they agree, down to a -0 and a NaN.
#include "doctest.h"
#include "cliApi.h"
#include "linesCodec.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
  void stencil_snapCropRect(double, double, double, double, double, double, double*);
  void stencil_rotateEditQuarter(double, double, double, double, int, double, double, int,
                                 double*);
  void stencil_cropChange(double, double, double, double, double, double, double, double,
                          double*);
  void stencil_mirrorEdit(double, double, double, double, int, double, double, double*);
  int stencil_mergeLinesKeep(const double*, int, const std::uint8_t*, int, const double*, int,
                             const std::uint8_t*, int, std::uint8_t*, int);
}

namespace {
  bool sameBits(const double* a, const double* b, int n) {
    return std::memcmp(a, b, sizeof(double) * static_cast<std::size_t>(n)) == 0;
  }

  struct Encoded {
    std::vector<double> nums;
    std::vector<std::uint8_t> text;
  };
  Encoded encode(const stencil::core::Lines& lines) {
    const stencil::core::abi::LinesSize s = stencil::core::abi::linesSize(lines);
    Encoded e{std::vector<double>(static_cast<std::size_t>(s.nums)),
              std::vector<std::uint8_t>(static_cast<std::size_t>(s.text) + 1)};
    stencil::core::abi::encodeLines(lines, e.nums.data(), e.text.data());
    return e;
  }
  stencil::core::Line at(double x, const char* color) {
    stencil::core::Line l;
    l.points = {{x, 0}};
    l.color = color;
    return l;
  }
}  // namespace

TEST_CASE("abi shared: snapCropRect agrees under both spellings") {
  const double rects[][4] = {{-5, -5, 999, 999}, {190.4, 2.6, 20.5, 10.2}, {-0.4, 0, 3, 3},
                             {NAN, 1, 2, 3}};
  for (const auto& r : rects) {
    double wasm[4] = {}, cli[4] = {};
    stencil_snapCropRect(r[0], r[1], r[2], r[3], 200, 100, wasm);
    stencil_cli_snapCropRect(r[0], r[1], r[2], r[3], 200, 100, cli);
    CHECK(sameBits(wasm, cli, 4));
  }
  stencil_snapCropRect(0, 0, 1, 1, 10, 10, nullptr);  // a null out slot is a no-op
  stencil_cli_snapCropRect(0, 0, 1, 1, 10, 10, nullptr);
}

TEST_CASE("abi shared: rotateEditQuarter agrees under both spellings") {
  for (int q = -1; q <= 4; ++q) {
    for (int cw = 0; cw <= 1; ++cw) {
      double wasm[5] = {}, cli[5] = {};
      stencil_rotateEditQuarter(0.4, 7.6, 33.5, 90, q, 200, 100, cw, wasm);
      stencil_cli_rotateEditQuarter(0.4, 7.6, 33.5, 90, q, 200, 100, cw, cli);
      CHECK(sameBits(wasm, cli, 5));
      CHECK(wasm[4] >= 0);
      CHECK(wasm[4] <= 3);
    }
  }
}

TEST_CASE("abi shared: mirrorEdit agrees under both spellings") {
  for (int q = -1; q <= 4; ++q) {
    double wasm[5] = {}, cli[5] = {};
    stencil_mirrorEdit(0.4, 7.6, 33.5, 90, q, 200, 100, wasm);
    stencil_cli_mirrorEdit(0.4, 7.6, 33.5, 90, q, 200, 100, cli);
    CHECK(sameBits(wasm, cli, 5));
    CHECK(wasm[4] >= 0);
    CHECK(wasm[4] <= 3);
  }
  stencil_cli_mirrorEdit(0, 0, 1, 1, 0, 10, 10, nullptr);
}

TEST_CASE("abi shared: cropChange agrees under both spellings") {
  const double pairs[][8] = {{0, 0, 16, 12, 4, 3, 8, 6},   {0, 0, 16, 12, 0, 0, 8, 12},
                             {0, 0, 16, 12, 0, 0, 12, 12}, {0, 0, 0, 5, 0, 0, 3, 7},
                             {0, 0, -4, -9, 0, 0, 2, 3},   {5, 5, 100, 141, 0, 0, 200, 282}};
  for (const auto& p : pairs) {
    double wasm[2] = {}, cli[2] = {};
    stencil_cropChange(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], wasm);
    stencil_cli_cropChange(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], cli);
    CHECK(sameBits(wasm, cli, 2));
  }
  double out[2] = {};
  stencil_cli_cropChange(0, 0, 16, 12, 0, 0, 8, 6, out);
  CHECK(out[0] == 0.0);
  CHECK(out[1] == 0.5);
  stencil_cli_cropChange(0, 0, 16, 12, 0, 0, 8, 12, out);  // album -> portrait: a flip
  CHECK(out[0] == 1.0);
  CHECK(out[1] == 1.0);
  stencil_cropChange(0, 0, 1, 1, 0, 0, 1, 1, nullptr);  // a null out slot is a no-op
  stencil_cli_cropChange(0, 0, 1, 1, 0, 0, 1, 1, nullptr);
}

TEST_CASE("abi shared: mergeLinesKeep agrees under both spellings") {
  const Encoded s = encode({at(1, "#f00")});
  const Encoded l = encode({at(1, "#f00"), at(2, "#f00"), at(2, "#f00")});
  using Fn = int (*)(const double*, int, const std::uint8_t*, int, const double*, int,
                     const std::uint8_t*, int, std::uint8_t*, int);
  for (Fn merge : {static_cast<Fn>(stencil_mergeLinesKeep), static_cast<Fn>(stencil_cli_mergeLinesKeep)}) {
    std::uint8_t keep[3] = {9, 9, 9};
    CHECK(merge(s.nums.data(), static_cast<int>(s.nums.size()), s.text.data(),
                static_cast<int>(s.text.size()), l.nums.data(), static_cast<int>(l.nums.size()),
                l.text.data(), static_cast<int>(l.text.size()), keep, 3) == 3);
    CHECK(keep[0] == 0);
    CHECK(keep[1] == 1);
    CHECK(keep[2] == 0);
    std::uint8_t capped[1] = {9};
    CHECK(merge(nullptr, 0, nullptr, 0, l.nums.data(), static_cast<int>(l.nums.size()),
                l.text.data(), static_cast<int>(l.text.size()), capped, 1) == 3);
    CHECK(capped[0] == 1);
    CHECK(merge(nullptr, 0, nullptr, 0, l.nums.data(), static_cast<int>(l.nums.size()),
                l.text.data(), static_cast<int>(l.text.size()), nullptr, 0) == 3);
  }
}

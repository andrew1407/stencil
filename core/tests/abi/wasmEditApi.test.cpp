// The wasmEditApi.cpp exports driven natively through their extern "C" prototypes: the merge
// mask over the lines codec, and the chain edits over flat point arrays.
#include "doctest.h"
#include "linesCodec.hpp"

#include <cstdint>
#include <vector>

using namespace stencil::core;

extern "C" {
  int stencil_mergeLinesKeep(const double*, int, const std::uint8_t*, int, const double*, int,
                             const std::uint8_t*, int, std::uint8_t*, int);
  int stencil_chainUnchain(const double*, int, int, double*);
  int stencil_chainPullOut(const double*, int, int, int, int, double, double, double*, int*);
}

namespace {
  struct Encoded {
    std::vector<double> nums;
    std::vector<std::uint8_t> text;
  };
  Encoded encode(const Lines& lines) {
    const abi::LinesSize s = abi::linesSize(lines);
    Encoded e{std::vector<double>(static_cast<std::size_t>(s.nums)),
              std::vector<std::uint8_t>(static_cast<std::size_t>(s.text) + 1)};
    abi::encodeLines(lines, e.nums.data(), e.text.data());
    return e;
  }
  Line at(double x, const char* color) {
    Line l;
    l.points = {{x, 0}};
    l.color = color;
    return l;
  }
}  // namespace

TEST_CASE("wasm edit abi: the merge mask marks the local lines that join") {
  const Encoded s = encode({at(1, "#f00")});
  const Encoded l = encode({at(1, "#f00"), at(2, "#f00"), at(2, "#f00")});
  std::uint8_t keep[3] = {9, 9, 9};
  const int n = stencil_mergeLinesKeep(s.nums.data(), static_cast<int>(s.nums.size()), s.text.data(),
                                       static_cast<int>(s.text.size()), l.nums.data(),
                                       static_cast<int>(l.nums.size()), l.text.data(),
                                       static_cast<int>(l.text.size()), keep, 3);
  CHECK(n == 3);
  CHECK(keep[0] == 0);
  CHECK(keep[1] == 1);
  CHECK(keep[2] == 0);
  CHECK(stencil_mergeLinesKeep(nullptr, 0, nullptr, 0, l.nums.data(), static_cast<int>(l.nums.size()),
                               l.text.data(), static_cast<int>(l.text.size()), nullptr, 0) == 3);
}

TEST_CASE("wasm edit abi: unchain and pull-out over flat point arrays") {
  const double closed[] = {0, 0, 10, 0, 10, 10, 0, 0};
  double out[10] = {};
  CHECK(stencil_chainUnchain(closed, 4, 1, out) == 3);
  CHECK(out[4] == 10);
  CHECK(out[5] == 10);
  CHECK(stencil_chainUnchain(closed, 4, 0, out) == -1);
  int count = 0;
  CHECK(stencil_chainPullOut(closed, 4, 1, 1, 1, 11, 4, out, &count) == 3);
  CHECK(count == 4);
  CHECK(out[0] == 10);
  CHECK(out[6] == 10);
  CHECK(stencil_chainPullOut(closed, 4, 0, 1, 9, 0, 0, out, &count) == -1);
  CHECK(count == 4);
}

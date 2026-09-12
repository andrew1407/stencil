#pragma once
// Shared timing/fixture helpers for the opt-in `bench` suite (tests/bench*.test.cpp).
// Header-only so the scaffolding needs no STENCIL_CORE_SOURCES entry.
#include <chrono>
#include <cstdint>
#include <vector>

namespace bench {

  // Wall-clock of one invocation, in milliseconds.
  template <class F>
  double time_ms(F&& f) {
    const auto t0 = std::chrono::steady_clock::now();
    f();
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
  }

  // Best (min) of `reps` runs — drops scheduler noise so the ceilings stay stable.
  template <class F>
  double best_ms(int reps, F&& f) {
    double best = 1e300;
    for (int i = 0; i < reps; ++i) {
      const double ms = time_ms(f);
      if (ms < best) best = ms;
    }
    return best;
  }

  // A non-flat RGBA8 image so bw/sepia do real arithmetic and contour finds edges.
  inline std::vector<std::uint8_t> gradient(int w, int h) {
    std::vector<std::uint8_t> b(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
        b[i + 0] = static_cast<std::uint8_t>(x);
        b[i + 1] = static_cast<std::uint8_t>(y);
        b[i + 2] = static_cast<std::uint8_t>(x ^ y);
        b[i + 3] = 255;
      }
    }
    return b;
  }

  // Sum a buffer so the optimizer can't elide the work we just timed.
  inline std::uint64_t checksum(const std::vector<std::uint8_t>& b) {
    std::uint64_t s = 0;
    for (auto v : b) s += v;
    return s;
  }

}  // namespace bench

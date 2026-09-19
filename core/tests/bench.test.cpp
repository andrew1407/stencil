// Performance-regression benchmarks for the core's per-pixel / per-element hotspots.
// DECORATED doctest::skip(), so ctest never runs them; on demand:
//     core/build/stencil_tests -ts=bench --no-skip
// Assertions are RELATIVE (ratios, or scaling as input doubles) with generous ceilings, so they
// catch algorithmic regressions, not noise. Twins: benchGeometry and benchLogic.test.cpp.
#include "doctest.h"

#include "benchSupport.hpp"  // time_ms / best_ms / gradient / checksum
#include "HistoryStack.hpp"
#include "imageFilter.hpp"
#include "imageOps.hpp"
#include "rasterize.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace stencil::core;
using namespace bench;

TEST_SUITE("bench") {

  // -- Large-image filters ------------------------------------------------------
  // Contour reads a 3x3 neighbourhood + Sobel; guard it stays a sane multiple of cheap bw.
  TEST_CASE("bench: large-image filters (bw / sepia / contour)" * doctest::skip()) {
    const int w = 3000, h = 2000;  // 6 MP, ~ a phone photo
    const double mp = (static_cast<double>(w) * h) / 1e6;
    auto base = gradient(w, h);

    auto buf = base;
    const double bw = best_ms(3, [&] {
      buf = base;
      applyFilterRGBA(FilterMode::BW, buf.data(), static_cast<std::size_t>(w) * h, 0, 0, 0);
    });
    const double sepia = best_ms(3, [&] {
      buf = base;
      applyFilterRGBA(FilterMode::SEPIA, buf.data(), static_cast<std::size_t>(w) * h, 0, 0, 0);
    });
    const double contour = best_ms(3, [&] {
      buf = base;
      applyContourRGBA(buf.data(), w, h);
    });
    volatile std::uint64_t sink = checksum(buf);
    (void)sink;

    MESSAGE("filters @ " << mp << " MP  bw=" << bw << "ms (" << mp / bw * 1000 << " MP/s)"
                         << "  sepia=" << sepia << "ms  contour=" << contour << "ms ("
                         << mp / contour * 1000 << " MP/s)");
    CHECK(bw > 0.0);
    CHECK(contour < bw * 80.0);   // contour is ~10-20x bw; 80x = generous regression ceiling
    CHECK(sepia < bw * 12.0);     // sepia is a 3x3 matrix; a few x bw at most
  }

  // -- Large-image geometry (crop / rotate) ------------------------------------
  // Crop is a per-row memcpy, rotate a tiled transpose (~10-15x dearer); guard the ratio.
  TEST_CASE("bench: large-image crop + quarter-turn rotate" * doctest::skip()) {
    const int w = 4000, h = 3000;  // 12 MP
    const double mp = (static_cast<double>(w) * h) / 1e6;
    auto src = gradient(w, h);

    std::vector<std::uint8_t> dst(static_cast<std::size_t>(w) * h * 4);
    const double crop = best_ms(3, [&] {
      cropImageRGBA(src.data(), w, h, 0, 0, w, h, dst.data());
    });

    int rw = 0, rh = 0;
    rotatedDims(w, h, 1, rw, rh);
    std::vector<std::uint8_t> rdst(static_cast<std::size_t>(rw) * rh * 4);
    const double rot = best_ms(3, [&] {
      rotateImageRGBA(src.data(), w, h, 1, rdst.data());
    });
    volatile std::uint64_t sink = checksum(dst) + checksum(rdst);
    (void)sink;

    MESSAGE("geometry @ " << mp << " MP  crop=" << crop << "ms (" << mp / crop * 1000
                          << " MP/s)  rotate90=" << rot << "ms (" << mp / rot * 1000 << " MP/s)");
    CHECK(crop > 0.0);
    CHECK(rot < crop * 40.0);  // ~12x when tiled; 40x = the untiled-transpose regression
  }

  // -- Many drawn lines (CLI / pystencil rasteriser) ---------------------------
  // Cost scales with total stroked length; assert LINEAR scaling in line count (no O(n^2)).
  TEST_CASE("bench: rasterize many lines scales linearly" * doctest::skip()) {
    const int w = 2000, h = 2000;

    auto make_lines = [](int n) {
      Lines lines;
      lines.reserve(static_cast<std::size_t>(n));
      for (int i = 0; i < n; ++i) {
        Line ln;
        // Deterministic pseudo-scatter of short 3-segment polylines across the canvas.
        const double bx = (i * 37) % 1900;
        const double by = (i * 53) % 1900;
        ln.points = {{bx, by}, {bx + 40, by + 15}, {bx + 10, by + 60}, {bx + 70, by + 70}};
        ln.color = "#3366ff";
        ln.thickness = 3;
        ln.pointSize = 4;
        ln.style = "solid";
        lines.push_back(ln);
      }
      return lines;
    };

    const int n1 = 2000;
    const Lines a = make_lines(n1);
    const Lines b = make_lines(n1 * 2);

    std::vector<std::uint8_t> buf(static_cast<std::size_t>(w) * h * 4, 0);
    const double t1 = best_ms(3, [&] {
      std::fill(buf.begin(), buf.end(), std::uint8_t{0});
      rasterizeLines(buf.data(), w, h, a);
    });
    const double t2 = best_ms(3, [&] {
      std::fill(buf.begin(), buf.end(), std::uint8_t{0});
      rasterizeLines(buf.data(), w, h, b);
    });
    volatile std::uint64_t sink = checksum(buf);
    (void)sink;

    MESSAGE("rasterize " << n1 << " lines=" << t1 << "ms (" << n1 / t1 * 1000
                         << " lines/s)  " << n1 * 2 << " lines=" << t2 << "ms  ratio="
                         << t2 / t1);
    CHECK(t1 > 0.0);
    CHECK(t2 < t1 * 3.0);  // 2x work should be ~2x time; 3x = generous linear-scaling ceiling
  }

  // -- Editing-session history growth -----------------------------------------
  // push() must stay O(snapshot), NOT O(history), or a long session goes quadratic.
  TEST_CASE("bench: history push stays O(1) as the session grows" * doctest::skip()) {
    const int pushes = 8000;
    Lines snap;  // a modest, fixed-size edit snapshot
    for (int i = 0; i < 40; ++i) {
      Line ln;
      ln.points = {{double(i), 0}, {double(i) + 5, 10}};
      snap.push_back(ln);
    }

    HistoryStack hs;
    hs.reset(snap);  // non-empty base -> retained as history[0]

    const double firstHalf = time_ms([&] {
      for (int i = 0; i < pushes / 2; ++i) hs.push(snap);
    });
    const double secondHalf = time_ms([&] {
      for (int i = 0; i < pushes / 2; ++i) hs.push(snap);
    });

    // Deterministic invariant: the stack saturates at the depth cap, it does not grow.
    CHECK(hs.size() == std::min(static_cast<std::size_t>(pushes) + 1, HistoryStack::MAX_STEPS));

    MESSAGE("history push x" << pushes << "  firstHalf=" << firstHalf << "ms  secondHalf="
                            << secondHalf << "ms  slowdown=" << secondHalf / firstHalf
                            << "x  final size=" << hs.size());
    // If push copied the whole history each time (O(n)), the second half — operating on
    // a deeper stack — would be several times slower. Amortised O(1) keeps it flat.
    CHECK(secondHalf < firstHalf * 4.0 + 1.0);  // +1ms cushions sub-ms first halves
  }

}  // TEST_SUITE("bench")

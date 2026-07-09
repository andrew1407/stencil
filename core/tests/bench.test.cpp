// Performance-regression benchmarks for the core's heavy per-pixel / per-element
// hotspots — the "large image / many lines / long editing session" paths that three
// front-ends (CLI, wasm browser, Qt desktop) all inherit from here.
//
// These live in the same stencil_tests binary but are DECORATED `doctest::skip()`, so
// the normal `ctest` run (and therefore CI) never executes them — no timing assertion
// ever gates a merge on a loaded shared runner. Run them on demand:
//
//     core/build/stencil_tests -ts=bench --no-skip
//     core/build/stencil_tests -ts=bench --no-skip -tc="*rasterize*"   # one case
//
// Every case prints a throughput line via MESSAGE. Assertions are deliberately
// RELATIVE (ratios between two ops, or scaling of one op as its input doubles) with
// generous ceilings: they catch algorithmic / order-of-magnitude regressions
// (e.g. an O(n) push turning a session O(n^2)), not micro-tuning noise. The one
// absolute-ish check is a deterministic invariant on HistoryStack size.
#include "doctest.h"

#include "historyStack.hpp"
#include "imageFilter.hpp"
#include "imageOps.hpp"
#include "rasterize.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

using namespace stencil::core;

namespace {

  // Wall-clock of one invocation, in milliseconds.
  template <class F>
  double time_ms(F&& f) {
    const auto t0 = std::chrono::steady_clock::now();
    f();
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
  }

  // Best (min) of `reps` runs — the robust "how fast can it go" estimator, which drops
  // scheduler noise spikes so the relative ceilings below stay stable across machines.
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
  std::vector<std::uint8_t> gradient(int w, int h) {
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
  std::uint64_t checksum(const std::vector<std::uint8_t>& b) {
    std::uint64_t s = 0;
    for (auto v : b) s += v;
    return s;
  }

}  // namespace

TEST_SUITE("bench") {

  // ── Large-image filters ─────────────────────────────────────────────────────
  // bw is a handful of ops/pixel; contour reads a 3x3 neighbourhood + Sobel, so it is
  // the most expensive filter. Guard that contour stays within a sane multiple of the
  // cheap path — a big jump means the neighbourhood loop regressed.
  TEST_CASE("bench: large-image filters (bw / sepia / contour)" * doctest::skip()) {
    const int w = 3000, h = 2000;  // 6 MP, ~ a phone photo
    const double mp = (static_cast<double>(w) * h) / 1e6;
    auto base = gradient(w, h);

    auto buf = base;
    const double bw = best_ms(3, [&] {
      buf = base;
      applyFilterRGBA(FilterMode::Bw, buf.data(), static_cast<std::size_t>(w) * h, 0, 0, 0);
    });
    const double sepia = best_ms(3, [&] {
      buf = base;
      applyFilterRGBA(FilterMode::Sepia, buf.data(), static_cast<std::size_t>(w) * h, 0, 0, 0);
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

  // ── Large-image geometry (crop / rotate) ───────────────────────────────────
  // Both are whole-image per-pixel copies; rotate does extra index math but must stay
  // in the same order of magnitude as a straight crop copy.
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
    CHECK(rot < crop * 10.0);  // rotate's strided writes cost more, but not an order beyond crop
  }

  // ── Many drawn lines (CLI / pystencil rasteriser) ──────────────────────────
  // The software rasteriser stamps discs along every polyline — cost scales with the
  // total stroked length. This is the "huge layout" hotspot and is NOT a wasm path
  // (the browser draws with canvas). Assert LINEAR scaling: doubling the line count
  // must not more-than-double the time, i.e. no accidental O(n^2).
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
        ln.markerSize = 4;
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

  // ── Editing-session history growth ─────────────────────────────────────────
  // HistoryStack keeps a full Lines snapshot per push with NO size cap (see
  // historyStack.hpp) — memory grows with edit count. That growth is by design; what
  // MUST stay true is that push() is O(snapshot), NOT O(history): the amortised cost of
  // pushing must not climb as the stack deepens, or a long session goes quadratic.
  TEST_CASE("bench: history push stays O(1) as the session grows" * doctest::skip()) {
    const int pushes = 8000;
    Lines snap;  // a modest, fixed-size edit snapshot
    for (int i = 0; i < 40; ++i) {
      Line ln;
      ln.points = {{double(i), 0}, {double(i) + 5, 10}};
      snap.push_back(ln);
    }

    HistoryStack hs;
    hs.reset(snap);  // non-empty base -> retained as history[0], so size == base + pushes

    const double firstHalf = time_ms([&] {
      for (int i = 0; i < pushes / 2; ++i) hs.push(snap);
    });
    const double secondHalf = time_ms([&] {
      for (int i = 0; i < pushes / 2; ++i) hs.push(snap);
    });

    // Deterministic invariant: every push retained (base + `pushes` snapshots).
    CHECK(hs.size() == static_cast<std::size_t>(pushes) + 1);

    MESSAGE("history push x" << pushes << "  firstHalf=" << firstHalf << "ms  secondHalf="
                            << secondHalf << "ms  slowdown=" << secondHalf / firstHalf
                            << "x  final size=" << hs.size());
    // If push copied the whole history each time (O(n)), the second half — operating on
    // a deeper stack — would be several times slower. Amortised O(1) keeps it flat.
    CHECK(secondHalf < firstHalf * 4.0 + 1.0);  // +1ms cushions sub-ms first halves
  }

}  // TEST_SUITE("bench")

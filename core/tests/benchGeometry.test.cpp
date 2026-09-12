// Geometry half of the opt-in `bench` suite — design and usage: bench.test.cpp.
#include "doctest.h"

#include "benchSupport.hpp"
#include "colorNames.hpp"
#include "hitTest.hpp"
#include "models.hpp"
#include "rasterize.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace stencil::core;
using namespace bench;

namespace {

  Line stroke(double len, double thick) {  // pointSize 0 keeps the handles out
    Line ln;
    ln.points = {{200.0, 200.0}, {200.0 + len, 200.0 + len * 0.5}};
    ln.color = "#3366ff";
    ln.thickness = thick;
    ln.pointSize = 0.0;
    ln.style = "solid";
    return ln;
  }

  // Closed ellipse of `edges` vertices — same area at any count, isolating the walk.
  std::vector<Point> ring(double cx, double cy, double rx, double ry, int edges) {
    std::vector<Point> pts;
    pts.reserve(static_cast<std::size_t>(edges));
    for (int i = 0; i < edges; ++i) {
      const double t = 6.283185307179586 * i / edges;
      pts.push_back({cx + rx * std::cos(t), cy + ry * std::sin(t)});
    }
    return pts;
  }

}  // namespace

TEST_SUITE("bench") {

  // ── Stroke cost: length x thickness^2 ──────────────────────────────────────
  // strokePolyline stamps an AA disc every 0.5 px over a (thickness+2)^2 box, so one
  // line costs O(length * thickness^2). bench.test.cpp holds both constant, hiding a
  // regression in either — and the disc-px/s here is the number that decides whether a
  // span-based stroke rewrite pays.
  TEST_CASE("bench: rasterize stroke scales with length and thickness^2" * doctest::skip()) {
    const int w = 4000, h = 4000;
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(w) * h * 4, 0);
    auto run = [&](const Line& ln) {
      return best_ms(3, [&] { rasterizeLine(buf.data(), w, h, ln); });
    };

    const double len1 = 1500.0, len2 = 3000.0;
    const double base = run(stroke(len1, 4.0));
    const double longer = run(stroke(len2, 4.0));
    const double thick2 = run(stroke(len1, 8.0));
    const double thick4 = run(stroke(len1, 16.0));
    volatile std::uint64_t sink = checksum(buf);
    (void)sink;

    // Stamps = 2 per px of path; each covers ~(thickness+2)^2 px of the AA box.
    const double stamps = 2.0 * len1 * std::sqrt(1.25);
    const double discPx = stamps * 36.0;  // thickness 4 -> (4+2)^2
    MESSAGE("stroke len=" << len1 << " t=4 -> " << base << "ms (" << discPx / base / 1e3
                          << " M disc-px/s)  len x2=" << longer << "ms ratio=" << longer / base
                          << "  t x2=" << thick2 << "ms ratio=" << thick2 / base
                          << "  t x4=" << thick4 << "ms ratio=" << thick4 / base);
    CHECK(base > 0.0);
    CHECK(longer < base * 3.0);   // linear in length; 3x = generous ceiling
    CHECK(thick2 < base * 8.0);   // quadratic in thickness: ~4x expected
    CHECK(thick4 < base * 40.0);  // ~16x expected; 40x catches a cubic regression
  }

  // ── Scanline fill ──────────────────────────────────────────────────────────
  // fillPolygonRows re-walks EVERY edge on EVERY scanline (no active-edge table):
  // O(rows * edges) on top of the span blending. A fat ellipse (few edges) gives the
  // blend throughput; a tall narrow one (many rows, ~6 px spans) isolates the walk.
  TEST_CASE("bench: fillPolygon edge walk and span blending" * doctest::skip()) {
    const int w = 2000, h = 2000;
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(w) * h * 4, 0);
    const Rgba c{60, 120, 255, 255};
    auto fill = [&](const std::vector<Point>& pts) {
      return best_ms(3, [&] { fillPolygonRows(buf.data(), w, h, pts, c, 0, h); });
    };

    const auto fat = ring(1000, 1000, 950, 950, 8);
    const double area = 3.14159265358979 * 950 * 950;
    const double tFat = fill(fat);

    const int e1 = 512;  // sliver: ~6 px wide over 1980 rows, so the walk dominates
    const double tE1 = fill(ring(1000, 1000, 3, 990, e1));
    const double tE2 = fill(ring(1000, 1000, 3, 990, e1 * 2));
    volatile std::uint64_t sink = checksum(buf);
    (void)sink;

    MESSAGE("fill area=" << area / 1e6 << " MP in " << tFat << "ms (" << area / tFat / 1e3
                         << " M px/s)  sliver " << e1 << " edges=" << tE1 << "ms  " << e1 * 2
                         << " edges=" << tE2 << "ms  ratio=" << tE2 / tE1 << "  ("
                         << (1980.0 * e1) / tE1 / 1e3 << " M edge-tests/s)");
    CHECK(tFat > 0.0);
    CHECK(tE1 > 0.0);
    CHECK(tE2 < tE1 * 3.0);  // linear in edges; > 3x means the per-scanline walk got worse
  }

  // ── Hit tests on every mouse-move ──────────────────────────────────────────
  // The desktop calls findLineAt + findNearestSegment 2-3x per mouse-move over all
  // lines x all points. The probe lattice mostly misses and sometimes hits — the real
  // mix, and the one a bbox early reject speeds up. Public API only, so it measures
  // whatever the implementation is; the assertion is scaling in line count.
  TEST_CASE("bench: findLineAt / findNearestSegment over a big layout" * doctest::skip()) {
    auto layout = [](int n) {
      Lines lines;
      lines.reserve(static_cast<std::size_t>(n));
      for (int i = 0; i < n; ++i) {
        Line ln;
        const double bx = (i * 37) % 1900, by = (i * 53) % 1900;
        for (int k = 0; k < 20; ++k)
          ln.points.push_back({bx + k * 3.0, by + ((k * 7) % 40)});
        lines.push_back(ln);
      }
      return lines;
    };
    const int n = 1500;
    const Lines a = layout(n), b = layout(n * 2);

    const int probes = 40 * 40;
    auto sweep = [&](const Lines& ls) {
      return best_ms(3, [&] {
        long long acc = 0;
        for (int i = 0; i < probes; ++i) {
          const double x = (i % 40) * 50.0 + 7.0, y = (i / 40) * 50.0 + 11.0;
          acc += findLineAt(ls, x, y);
          acc += findNearestSegment(ls, x, y) ? 1 : 0;
        }
        volatile long long sink = acc;
        (void)sink;
      });
    };
    const double t1 = sweep(a);
    const double t2 = sweep(b);

    MESSAGE("hit-test " << n << " lines x20 pts: " << t1 << "ms / " << probes << " probes ("
                        << t1 / probes * 1000.0 << " us per mouse-move)  " << n * 2
                        << " lines=" << t2 << "ms  ratio=" << t2 / t1);
    CHECK(t1 > 0.0);
    CHECK(t2 < t1 * 3.0);  // linear in line count; 3x = generous ceiling
  }

}  // TEST_SUITE("bench")

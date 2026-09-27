#pragma once
// The disc stamper the coverage rasteriser replaced, kept as the tests' reference: a full AA disc
// blended every 0.5 px of path, O(length x thickness^2), where translucent ink piles up. Shared by
// the timed bench (benchGeometry.test.cpp) and the counted proof (strokeCoverage.test.cpp).
#include "colorNames.hpp"
#include "models.hpp"
#include "pixelBlend.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace discStamp {

  using stencil::core::Line;
  using stencil::core::Point;

  // The speed-up scene: a 64 px translucent polyline across a 2400 x 1800 image.
  constexpr int SCENE_W = 2400, SCENE_H = 1800;
  inline Line scene() {
    Line l;
    l.points = {{100, 100}, {2300, 1700}, {300, 1500}};
    l.color = "#3366ff80";
    l.thickness = 64.0;
    l.pointSize = 0.0;
    l.style = "solid";
    return l;
  }

  // One stamp centre every 0.5 px of each segment, ends included.
  template <class F>
  void eachStamp(const Line& ln, F f) {
    for (std::size_t i = 0; i + 1 < ln.points.size(); ++i) {
      const Point a = ln.points[i], b = ln.points[i + 1];
      const double len = std::hypot(b.x - a.x, b.y - a.y);
      const int n = std::max(1, static_cast<int>(std::ceil(len / 0.5)));
      for (int s = 0; s <= n; ++s) f(a.x + (b.x - a.x) * s / n, a.y + (b.y - a.y) * s / n);
    }
  }

  // A stamp's inclusive pixel box on one axis, around centre `c` for radius `r`.
  inline int lo(double c, double r) { return static_cast<int>(c - r - 1); }
  inline int hi(double c, double r) { return static_cast<int>(c + r + 1); }

  inline void stampStroke(std::uint8_t* buf, int w, int h, const Line& ln) {
    const stencil::core::Rgba c = *stencil::core::parseColor(ln.color);
    const double r = ln.thickness * 0.5;
    eachStamp(ln, [&](double cx, double cy) {
      for (int y = lo(cy, r); y <= hi(cy, r); ++y)
        for (int x = lo(cx, r); x <= hi(cx, r); ++x) {
          const double cov = r + 0.5 - std::hypot(x + 0.5 - cx, y + 0.5 - cy);
          if (cov > 0.0) stencil::core::blend::blendPixel(buf, w, h, x, y, c, std::min(cov, 1.0));
        }
    });
  }

  // The pixels stampStroke examines: every stamp's whole box, one distance test each.
  inline std::size_t visits(const Line& ln) {
    const double r = ln.thickness * 0.5;
    std::size_t n = 0;
    eachStamp(ln, [&](double cx, double cy) {
      n += static_cast<std::size_t>(hi(cy, r) - lo(cy, r) + 1) *
           static_cast<std::size_t>(hi(cx, r) - lo(cx, r) + 1);
    });
    return n;
  }

}  // namespace discStamp

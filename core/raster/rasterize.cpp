#include "rasterize.hpp"

#include "colorNames.hpp"
#include "markers.hpp"
#include "pixelBlend.hpp"
#include "strokeCoverage.hpp"
#include "text.hpp"  // Keyed, lookupPtr

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace stencil::core {

  namespace {

    using blend::blendPixel;
    using blend::clampToInt;

    // Largest coordinate / size a layout line may carry: unbounded untrusted input would overflow
    // the int casts and spin the scan loops. Lines beyond it are skipped as inert.
    constexpr double MAX_COORD = 1e6;

    // The browser's `line.style` words, matched case-sensitively as render.js does; any other
    // style strokes solid.
    constexpr std::array<Keyed<DashPattern>, 2> DASH_STYLES = {{
        {"dashed", DASHED},
        {"dotted", DOTTED},
    }};

    bool lineWithinBounds(const Line& line) {
      if (!std::isfinite(line.thickness) || std::abs(line.thickness) > MAX_COORD) return false;
      if (!std::isfinite(line.pointSize) || std::abs(line.pointSize) > MAX_COORD) return false;
      for (const Point& p : line.points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
        if (std::abs(p.x) > MAX_COORD || std::abs(p.y) > MAX_COORD) return false;
      }
      return true;
    }

  }  // namespace

  void rasterizeLine(std::uint8_t* buf, int w, int h, const Line& line) {
    if (line.points.empty()) return;
    // Untrusted layout: a non-finite or absurd value is skipped as inert, buffer intact
    // (the int casts and scan loops below assume the bound).
    if (!lineWithinBounds(line)) return;

    if (line.locked && line.points.size() >= 3) {
      if (const auto fill = parseColor(line.fillColor); fill && fill->a > 0)
        fillPolygonRows(buf, w, h, line.points, *fill, 0, h);
    }

    const auto stroke = parseColor(line.color);
    const bool strokeOn = stroke && stroke->a > 0;
    if (strokeOn)
      coverage::strokePolyline(buf, w, h, line.points, line.locked, line.thickness,
                               lookupPtr(DASH_STYLES, line.style), *stroke);
    // Markers (a disc under the editor's dark handle ring) do not ride on the stroke, as
    // canvas/Qt draw them either way; an unset point colour inherits `color`, though.
    if (line.pointSize <= 0.0) return;
    const auto own = parseColor(pointColorOr(line));
    const Rgba fill = (own && own->a > 0) ? *own : (strokeOn ? *stroke : Rgba{0, 0, 0, 0});
    if (fill.a == 0) return;
    // Capped as a stroke is.
    markers::drawMarkers(buf, w, h, line.points, std::min(line.pointSize, MAX_STROKE_THICKNESS), fill,
                         Rgba{0, 0, 0, CHANNEL_MAX});
  }

  void fillPolygonRows(std::uint8_t* buf, int w, int h, const std::vector<Point>& pts,
                       const Rgba& c, int rowFrom, int rowTo) {
    if (pts.size() < 3) return;
    double minY = pts[0].y, maxY = pts[0].y;
    for (const Point& p : pts) { minY = std::min(minY, p.y); maxY = std::max(maxY, p.y); }
    const int y0 = std::max(clampToInt(std::floor(minY), 0, h - 1), std::max(rowFrom, 0));
    const int y1 = std::min(clampToInt(std::ceil(maxY), 0, h - 1), std::min(rowTo, h) - 1);
    std::vector<double> xs;
    for (int y = y0; y <= y1; ++y) {
      const double sy = y + 0.5;
      xs.clear();
      for (std::size_t i = 0, nn = pts.size(); i < nn; ++i) {
        const Point& a = pts[i];
        const Point& b = pts[(i + 1) % nn];
        if ((a.y <= sy && b.y > sy) || (b.y <= sy && a.y > sy)) {
          const double t = (sy - a.y) / (b.y - a.y);
          xs.push_back(a.x + (b.x - a.x) * t);
        }
      }
      std::sort(xs.begin(), xs.end());
      for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {
        // Asymmetric clamp, NOT clampToInt: every x in [xa, xb] is blended unguarded, so a span wholly
        // off-canvas must stay EMPTY (xa > xb) - a symmetric clamp would paint a border stripe.
        const int xa = std::max(0, static_cast<int>(std::ceil(xs[i] - 0.5)));
        const int xb = std::min(w - 1, static_cast<int>(std::floor(xs[i + 1] - 0.5)));
        for (int x = xa; x <= xb; ++x) blendPixel(buf, w, h, x, y, c, 1.0);
      }
    }
  }

}  // namespace stencil::core

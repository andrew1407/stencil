#include "rasterize.hpp"

#include "colorNames.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace stencil::core {

  namespace {

    // Source-over blend one pixel. `coverage` (0..1) is the geometric coverage; it is
    // combined with the colour's own alpha. Out-of-bounds writes are ignored.
    void blendPixel(std::uint8_t* buf, int w, int h, int x, int y, const Rgba& c,
                    double coverage) {
      if (x < 0 || x >= w || y < 0 || y >= h) return;
      double ea = coverage * (c.a / 255.0);
      if (ea <= 0.0) return;
      if (ea > 1.0) ea = 1.0;
      std::uint8_t* p = buf + (static_cast<std::size_t>(y) * w + x) * 4;
      const double inv = 1.0 - ea;
      p[0] = static_cast<std::uint8_t>(std::lround(c.r * ea + p[0] * inv));
      p[1] = static_cast<std::uint8_t>(std::lround(c.g * ea + p[1] * inv));
      p[2] = static_cast<std::uint8_t>(std::lround(c.b * ea + p[2] * inv));
      p[3] = static_cast<std::uint8_t>(std::lround(255.0 * ea + p[3] * inv));
    }

    // Stamp a filled, anti-aliased disc of `radius` centred at (cx,cy).
    void stampDisc(std::uint8_t* buf, int w, int h, double cx, double cy,
                   double radius, const Rgba& c) {
      if (radius <= 0.0) return;
      const int x0 = static_cast<int>(std::floor(cx - radius - 1.0));
      const int x1 = static_cast<int>(std::ceil(cx + radius + 1.0));
      const int y0 = static_cast<int>(std::floor(cy - radius - 1.0));
      const int y1 = static_cast<int>(std::ceil(cy + radius + 1.0));
      for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
          const double dx = (x + 0.5) - cx;
          const double dy = (y + 0.5) - cy;
          const double d = std::sqrt(dx * dx + dy * dy);
          const double cov = std::clamp(radius + 0.5 - d, 0.0, 1.0);
          if (cov > 0.0) blendPixel(buf, w, h, x, y, c, cov);
        }
      }
    }

    // Stamp an anti-aliased ring (outline) of the given line width at `radius`.
    void stampRing(std::uint8_t* buf, int w, int h, double cx, double cy,
                   double radius, double lineWidth, const Rgba& c) {
      const double outer = radius + lineWidth * 0.5 + 1.0;
      const int x0 = static_cast<int>(std::floor(cx - outer));
      const int x1 = static_cast<int>(std::ceil(cx + outer));
      const int y0 = static_cast<int>(std::floor(cy - outer));
      const int y1 = static_cast<int>(std::ceil(cy + outer));
      for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
          const double dx = (x + 0.5) - cx;
          const double dy = (y + 0.5) - cy;
          const double d = std::sqrt(dx * dx + dy * dy);
          const double cov = std::clamp(lineWidth * 0.5 + 0.5 - std::abs(d - radius),
                                        0.0, 1.0);
          if (cov > 0.0) blendPixel(buf, w, h, x, y, c, cov);
        }
      }
    }

    // Is `pos` (arc length along the path) on an "ink" portion of the dash pattern?
    bool dashOn(double pos, const std::string& style, double thickness) {
      if (style == "dashed") {
        const double on = std::max(thickness * 3.0, 1.0);
        const double off = std::max(thickness * 2.0, 1.0);
        const double cycle = on + off;
        return std::fmod(pos, cycle) < on;
      }
      if (style == "dotted") {
        const double on = std::max(thickness, 1.0);
        const double off = std::max(thickness * 1.5, 1.0);
        const double cycle = on + off;
        return std::fmod(pos, cycle) < on;
      }
      return true;  // solid (and any unknown style)
    }

    // Stroke a polyline by stamping discs of radius `thickness/2` along it every ~0.5px,
    // skipping the gaps of dashed/dotted patterns.
    void strokePolyline(std::uint8_t* buf, int w, int h, const std::vector<Point>& pts,
                        bool closed, double thickness, const std::string& style,
                        const Rgba& c) {
      if (pts.size() < 2 || thickness <= 0.0) return;
      const double radius = std::max(thickness * 0.5, 0.5);
      const double step = 0.5;
      double pos = 0.0;
      const std::size_t segs = closed ? pts.size() : pts.size() - 1;
      for (std::size_t i = 0; i < segs; ++i) {
        const Point& a = pts[i];
        const Point& b = pts[(i + 1) % pts.size()];
        const double segLen = std::hypot(b.x - a.x, b.y - a.y);
        const int n = std::max(1, static_cast<int>(std::ceil(segLen / step)));
        for (int s = 0; s <= n; ++s) {
          const double u = static_cast<double>(s) / n;
          const double px = a.x + (b.x - a.x) * u;
          const double py = a.y + (b.y - a.y) * u;
          if (dashOn(pos + u * segLen, style, thickness))
            stampDisc(buf, w, h, px, py, radius, c);
        }
        pos += segLen;
      }
    }

    // Even-odd scanline fill of a closed polygon.
    void fillPolygon(std::uint8_t* buf, int w, int h, const std::vector<Point>& pts,
                     const Rgba& c) {
      if (pts.size() < 3) return;
      double minY = pts[0].y, maxY = pts[0].y;
      for (const Point& p : pts) { minY = std::min(minY, p.y); maxY = std::max(maxY, p.y); }
      const int y0 = std::max(0, static_cast<int>(std::floor(minY)));
      const int y1 = std::min(h - 1, static_cast<int>(std::ceil(maxY)));
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
          const int xa = std::max(0, static_cast<int>(std::ceil(xs[i] - 0.5)));
          const int xb = std::min(w - 1, static_cast<int>(std::floor(xs[i + 1] - 0.5)));
          for (int x = xa; x <= xb; ++x) blendPixel(buf, w, h, x, y, c, 1.0);
        }
      }
    }

  }  // namespace

  void rasterizeLine(std::uint8_t* buf, int w, int h, const Line& line) {
    if (line.points.empty()) return;

    // Fill first (a locked/closed area with a non-transparent fill colour).
    if (line.locked && line.points.size() >= 3) {
      if (const auto fill = parseColor(line.fillColor); fill && fill->a > 0)
        fillPolygon(buf, w, h, line.points, *fill);
    }

    // Stroke the polyline.
    if (const auto stroke = parseColor(line.color); stroke && stroke->a > 0) {
      strokePolyline(buf, w, h, line.points, line.locked, line.thickness, line.style,
                     *stroke);

      // Point markers: a filled disc in the line colour with a thin dark outline
      // (matching the editor's signature yellow-on-black handles).
      if (line.markerSize > 0.0) {
        const Rgba outline{0, 0, 0, 255};
        for (const Point& p : line.points) {
          stampDisc(buf, w, h, p.x, p.y, line.markerSize, *stroke);
          stampRing(buf, w, h, p.x, p.y, line.markerSize, 1.0, outline);
        }
      }
    }
  }

  void rasterizeLines(std::uint8_t* buf, int w, int h, const Lines& lines) {
    for (const Line& line : lines) rasterizeLine(buf, w, h, line);
  }

}  // namespace stencil::core

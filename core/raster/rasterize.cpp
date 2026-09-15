#include "rasterize.hpp"

#include "colorNames.hpp"
#include "rgba.hpp"  // rgbaOffset

#include <algorithm>
#include <cmath>
#include <vector>

namespace stencil::core {

  namespace {

    // Largest coordinate / size a layout line may carry: real images are a few thousand
    // px, and unbounded untrusted input would overflow the int casts and spin the
    // scan/step loops. Lines beyond it are skipped as inert.
    constexpr double MAX_COORD = 1e6;

    // `!(v >= lo)` also catches NaN. Clamping scan bounds is output-preserving (blendPixel
    // skips out-of-bounds writes) and caps the loop length of a far-off or huge stamp.
    inline int clampToInt(double v, int lo, int hi) {
      if (!(v >= static_cast<double>(lo))) return lo;
      if (v > static_cast<double>(hi)) return hi;
      return static_cast<int>(v);
    }

    bool lineWithinBounds(const Line& line) {
      if (!std::isfinite(line.thickness) || std::abs(line.thickness) > MAX_COORD) return false;
      if (!std::isfinite(line.pointSize) || std::abs(line.pointSize) > MAX_COORD) return false;
      for (const Point& p : line.points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
        if (std::abs(p.x) > MAX_COORD || std::abs(p.y) > MAX_COORD) return false;
      }
      return true;
    }

    // round(v / 255) for v in [0, 65535], without a divide. Exact over that range,
    // which covers c*a + d*(255-a) since the two weights sum to 255 (max 255*255).
    inline std::uint8_t div255(int v) {
      v += 128;  // round-to-nearest bias
      return static_cast<std::uint8_t>((v + (v >> 8)) >> 8);
    }

    // Source-over with `coverage` (0..1) folded into the colour's alpha, quantised to 8
    // bits; out-of-bounds writes are ignored.
    void blendPixel(std::uint8_t* buf, int w, int h, int x, int y, const Rgba& c,
                    double coverage) {
      if (x < 0 || x >= w || y < 0 || y >= h) return;
      int a = static_cast<int>(coverage * c.a + 0.5);  // effective alpha, 0..255
      if (a <= 0) return;
      if (a > 255) a = 255;
      const int ia = 255 - a;
      std::uint8_t* p = buf + rgbaOffset(x, y, w);
      p[0] = div255(c.r * a + p[0] * ia);
      p[1] = div255(c.g * a + p[1] * ia);
      p[2] = div255(c.b * a + p[2] * ia);
      p[3] = div255(255 * a + p[3] * ia);
    }

    void stampDisc(std::uint8_t* buf, int w, int h, double cx, double cy,
                   double radius, const Rgba& c) {
      if (radius <= 0.0) return;
      const int x0 = clampToInt(std::floor(cx - radius - 1.0), 0, w - 1);
      const int x1 = clampToInt(std::ceil(cx + radius + 1.0), 0, w - 1);
      const int y0 = clampToInt(std::floor(cy - radius - 1.0), 0, h - 1);
      const int y1 = clampToInt(std::ceil(cy + radius + 1.0), 0, h - 1);
      // Coverage = clamp(radius + 0.5 - d, 0, 1) needs the sqrt only in the 1px AA rim;
      // squared-distance culling of interior/exterior is byte-identical.
      const double rIn = radius - 0.5;
      const double rInSq = rIn > 0.0 ? rIn * rIn : -1.0;
      const double rOut = radius + 0.5;
      const double rOutSq = rOut * rOut;
      for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
          const double dx = (x + 0.5) - cx;
          const double dy = (y + 0.5) - cy;
          const double dsq = dx * dx + dy * dy;
          if (dsq >= rOutSq) continue;                  // exterior: cov == 0
          if (dsq <= rInSq) {                            // interior: cov == 1
            blendPixel(buf, w, h, x, y, c, 1.0);
            continue;
          }
          const double cov = radius + 0.5 - std::sqrt(dsq);  // rim only
          if (cov > 0.0) blendPixel(buf, w, h, x, y, c, cov);
        }
      }
    }

    void stampRing(std::uint8_t* buf, int w, int h, double cx, double cy,
                   double radius, double lineWidth, const Rgba& c) {
      const double outer = radius + lineWidth * 0.5 + 1.0;
      const int x0 = clampToInt(std::floor(cx - outer), 0, w - 1);
      const int x1 = clampToInt(std::ceil(cx + outer), 0, w - 1);
      const int y0 = clampToInt(std::floor(cy - outer), 0, h - 1);
      const int y1 = clampToInt(std::ceil(cy + outer), 0, h - 1);
      // Coverage is non-zero only in the band [radius - half, radius + half].
      const double half = lineWidth * 0.5 + 0.5;
      const double bandOut = radius + half;
      const double bandOutSq = bandOut * bandOut;
      const double bandIn = radius - half;
      const double bandInSq = bandIn > 0.0 ? bandIn * bandIn : -1.0;
      for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
          const double dx = (x + 0.5) - cx;
          const double dy = (y + 0.5) - cy;
          const double dsq = dx * dx + dy * dy;
          if (dsq >= bandOutSq || dsq <= bandInSq) continue;  // outside the band
          const double d = std::sqrt(dsq);
          const double cov = std::clamp(half - std::abs(d - radius), 0.0, 1.0);
          if (cov > 0.0) blendPixel(buf, w, h, x, y, c, cov);
        }
      }
    }

    // `pos` is arc length along the path.
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

    // Discs of radius thickness/2 stamped every ~0.5px, skipping dash gaps.
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
      strokePolyline(buf, w, h, line.points, line.locked, line.thickness, line.style,
                     *stroke);
    // Markers (a disc under the editor's dark handle ring) do not ride on the stroke, as
    // canvas/Qt draw them either way; an unset point colour inherits `color`, though.
    if (line.pointSize <= 0.0) return;
    const auto own = parseColor(pointColorOr(line));
    const Rgba fill = (own && own->a > 0) ? *own : (strokeOn ? *stroke : Rgba{0, 0, 0, 0});
    if (fill.a == 0) return;
    const Rgba outline{0, 0, 0, 255};
    for (const Point& p : line.points) {
      stampDisc(buf, w, h, p.x, p.y, line.pointSize, fill);
      stampRing(buf, w, h, p.x, p.y, line.pointSize, 1.0, outline);
    }
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
        // Asymmetric clamp, NOT clampToInt: every x in [xa, xb] is blended unguarded, so
        // a span wholly off-canvas must stay EMPTY (xa > xb) — clampToInt would collapse
        // both ends onto one edge pixel and paint a border stripe. xs are finite here.
        const int xa = std::max(0, static_cast<int>(std::ceil(xs[i] - 0.5)));
        const int xb = std::min(w - 1, static_cast<int>(std::floor(xs[i + 1] - 0.5)));
        for (int x = xa; x <= xb; ++x) blendPixel(buf, w, h, x, y, c, 1.0);
      }
    }
  }

  void rasterizeLines(std::uint8_t* buf, int w, int h, const Lines& lines) {
    for (const Line& line : lines) rasterizeLine(buf, w, h, line);
  }

}  // namespace stencil::core

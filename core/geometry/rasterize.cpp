#include "rasterize.hpp"

#include "colorNames.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace stencil::core {

  namespace {

    // Largest coordinate / size magnitude a layout line may carry. Real image
    // coordinates are a few thousand pixels; anything past this is non-physical
    // (and, unbounded, would overflow int casts and spin near-infinite scan/step
    // loops on untrusted layout input). Lines beyond it are skipped as inert.
    constexpr double kMaxCoord = 1e6;

    // Clamp a coordinate double to the inclusive pixel range [lo, hi] and cast to
    // int without UB. `!(v >= lo)` also catches NaN. Clamping scan bounds to the
    // buffer is output-preserving — blendPixel already skips out-of-bounds writes,
    // so the clamped-away iterations never drew anything — while capping the loop
    // length so a far-off-canvas or huge-radius stamp can't spin.
    inline int clampToInt(double v, int lo, int hi) {
      if (!(v >= static_cast<double>(lo))) return lo;
      if (v > static_cast<double>(hi)) return hi;
      return static_cast<int>(v);
    }

    // Reject a line whose points/sizes are non-finite or absurdly large before any
    // int cast or scan/step loop runs on them.
    bool lineWithinBounds(const Line& line) {
      if (!std::isfinite(line.thickness) || std::abs(line.thickness) > kMaxCoord) return false;
      if (!std::isfinite(line.markerSize) || std::abs(line.markerSize) > kMaxCoord) return false;
      for (const Point& p : line.points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
        if (std::abs(p.x) > kMaxCoord || std::abs(p.y) > kMaxCoord) return false;
      }
      return true;
    }

    // round(v / 255) for v in [0, 65535], without a divide. Exact over that range,
    // which covers c*a + d*(255-a) since the two weights sum to 255 (max 255*255).
    inline std::uint8_t div255(int v) {
      v += 128;  // round-to-nearest bias
      return static_cast<std::uint8_t>((v + (v >> 8)) >> 8);
    }

    // Source-over blend one pixel. `coverage` (0..1) is the geometric coverage, combined
    // with the colour's own alpha. Out-of-bounds writes are ignored. Integer fixed-point:
    // the effective alpha is quantised to 8 bits and channels blend via the divide-free
    // div255 above (one multiply-add per channel, no double math / lround), matching the
    // old double path to within ~1 LSB per blend.
    void blendPixel(std::uint8_t* buf, int w, int h, int x, int y, const Rgba& c,
                    double coverage) {
      if (x < 0 || x >= w || y < 0 || y >= h) return;
      int a = static_cast<int>(coverage * c.a + 0.5);  // effective alpha, 0..255
      if (a <= 0) return;
      if (a > 255) a = 255;
      const int ia = 255 - a;
      std::uint8_t* p = buf + (static_cast<std::size_t>(y) * w + x) * 4;
      p[0] = div255(c.r * a + p[0] * ia);
      p[1] = div255(c.g * a + p[1] * ia);
      p[2] = div255(c.b * a + p[2] * ia);
      p[3] = div255(255 * a + p[3] * ia);
    }

    // Stamp a filled, anti-aliased disc of `radius` centred at (cx,cy).
    void stampDisc(std::uint8_t* buf, int w, int h, double cx, double cy,
                   double radius, const Rgba& c) {
      if (radius <= 0.0) return;
      const int x0 = clampToInt(std::floor(cx - radius - 1.0), 0, w - 1);
      const int x1 = clampToInt(std::ceil(cx + radius + 1.0), 0, w - 1);
      const int y0 = clampToInt(std::floor(cy - radius - 1.0), 0, h - 1);
      const int y1 = clampToInt(std::ceil(cy + radius + 1.0), 0, h - 1);
      // Coverage = clamp(radius + 0.5 - d, 0, 1) only needs d in the 1px AA rim. Compare
      // squared distances to skip the sqrt for the full interior (cov 1) and empty exterior
      // (cov 0) — byte-identical to evaluating the formula at every pixel.
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

    // Stamp an anti-aliased ring (outline) of the given line width at `radius`.
    void stampRing(std::uint8_t* buf, int w, int h, double cx, double cy,
                   double radius, double lineWidth, const Rgba& c) {
      const double outer = radius + lineWidth * 0.5 + 1.0;
      const int x0 = clampToInt(std::floor(cx - outer), 0, w - 1);
      const int x1 = clampToInt(std::ceil(cx + outer), 0, w - 1);
      const int y0 = clampToInt(std::floor(cy - outer), 0, h - 1);
      const int y1 = clampToInt(std::ceil(cy + outer), 0, h - 1);
      // Non-zero coverage only within [radius - half, radius + half] of the centre,
      // where half = lineWidth/2 + 0.5. Cull the interior and outer field by squared
      // distance so the sqrt runs only on the ring band itself.
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
      const int y0 = clampToInt(std::floor(minY), 0, h - 1);
      const int y1 = clampToInt(std::ceil(maxY), 0, h - 1);
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
          // Asymmetric clamp, NOT clampToInt: this fill loop blends every x in
          // [xa, xb] unconditionally (no per-pixel coverage guard), so a span lying
          // wholly off-canvas must stay EMPTY (xa > xb). clampToInt would collapse
          // both ends onto the same edge pixel and paint a spurious border stripe.
          // (xs are finite: lineWithinBounds rejected non-finite/huge points up front.)
          const int xa = std::max(0, static_cast<int>(std::ceil(xs[i] - 0.5)));
          const int xb = std::min(w - 1, static_cast<int>(std::floor(xs[i + 1] - 0.5)));
          for (int x = xa; x <= xb; ++x) blendPixel(buf, w, h, x, y, c, 1.0);
        }
      }
    }

  }  // namespace

  void rasterizeLine(std::uint8_t* buf, int w, int h, const Line& line) {
    if (line.points.empty()) return;
    // Untrusted layout coords: a non-finite or absurd point/thickness/markerSize
    // would make the int casts below UB and spin the step/scan loops near-forever.
    // Such a line is skipped as inert (nothing drawn), leaving the buffer intact.
    if (!lineWithinBounds(line)) return;

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

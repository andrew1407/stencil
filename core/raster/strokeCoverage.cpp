#include "strokeCoverage.hpp"

#include "pixelBlend.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <utility>

namespace stencil::core::coverage {

  namespace {

    // One path segment clipped to the image grown by the ink's reach. `arc` is the path length at
    // `a`, so a dash keeps its phase from segment to segment.
    struct Segment {
      Point a, b;
      double arc = 0.0, len = 0.0;
      int top = 0, bottom = 0;  // rows it can touch
    };

    // Coverage is 1 within rIn of the path, radius + 0.5 - d in the 1 px rim, 0 past reach.
    struct Pen {
      double radius, rIn, reach;
      const DashPattern* dash;  // nullptr = solid
    };

    using Span = std::pair<int, int>;  // inclusive pixel columns

    // Liang-Barsky: the [t0, t1] of a + t(b - a) inside [lo, hiX] x [lo, hiY]; false if none.
    bool clip(const Point& a, const Point& b, double lo, double hiX, double hiY,
              double& t0, double& t1) {
      const double dx = b.x - a.x, dy = b.y - a.y;
      const double p[4] = {-dx, dx, -dy, dy};
      const double q[4] = {a.x - lo, hiX - a.x, a.y - lo, hiY - a.y};
      t0 = 0.0;
      t1 = 1.0;
      for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
          if (q[i] < 0.0) return false;
          continue;
        }
        const double r = q[i] / p[i];
        if (p[i] < 0.0) t0 = std::max(t0, r);
        else t1 = std::min(t1, r);
      }
      return t0 <= t1;
    }

    // Offset along `s` of the inked point nearest `along`; -1 when `s` lies wholly in a gap.
    // A dash cycle inks `on` px, then skips `off` px, from arc 0 (canvas setLineDash).
    double nearestInk(const Segment& s, const DashPattern* dash, double along) {
      const double u = std::clamp(along, 0.0, s.len);
      if (dash == nullptr) return u;
      const double cycle = dash->on + dash->off;
      const double m = std::fmod(s.arc + u, cycle);
      if (m <= dash->on) return u;
      const double prev = u - (m - dash->on), next = u + (cycle - m);
      const bool prevOk = prev >= 0.0, nextOk = next <= s.len;
      if (prevOk && nextOk) return along - prev <= next - along ? prev : next;
      return prevOk ? prev : (nextOk ? next : -1.0);
    }

    double coverageAt(const Segment& s, const Pen& pen, double px, double py) {
      const double vx = s.b.x - s.a.x, vy = s.b.y - s.a.y;
      double t = 0.0;
      if (s.len > 0.0) {
        const double at =
            nearestInk(s, pen.dash, ((px - s.a.x) * vx + (py - s.a.y) * vy) / s.len);
        if (at < 0.0) return 0.0;
        t = at / s.len;
      }
      const double dx = px - (s.a.x + t * vx), dy = py - (s.a.y + t * vy);
      const double dsq = dx * dx + dy * dy;
      if (dsq >= pen.reach * pen.reach) return 0.0;
      if (pen.rIn > 0.0 && dsq <= pen.rIn * pen.rIn) return 1.0;
      return pen.radius + 0.5 - std::sqrt(dsq);
    }

    // Pixel columns whose centres lie within `rho` of `s` on the row centred at `py`. A capsule
    // is convex, so its two end discs and its slab meet the row in one span.
    bool rowSpan(const Segment& s, double py, double rho, int w, Span& out) {
      double lo = HUGE_VAL, hi = -HUGE_VAL;
      for (const Point* e : {&s.a, &s.b}) {
        const double dy = py - e->y, q = rho * rho - dy * dy;
        if (q < 0.0) continue;
        lo = std::min(lo, e->x - std::sqrt(q));
        hi = std::max(hi, e->x + std::sqrt(q));
      }
      if (s.len > 0.0) {
        // Slab, in X = x - a.x: 0 <= X*vx + ry*vy <= len^2 and |ry*vx - X*vy| <= rho*len.
        const double vx = s.b.x - s.a.x, vy = s.b.y - s.a.y, ry = py - s.a.y;
        double a = -HUGE_VAL, b = HUGE_VAL;
        const auto bound = [&](double k, double c0, double mn, double mx) {
          if (k == 0.0) {
            if (c0 < mn || c0 > mx) a = HUGE_VAL;
            return;
          }
          const double l = (mn - c0) / k, r = (mx - c0) / k;
          a = std::max(a, std::min(l, r));
          b = std::min(b, std::max(l, r));
        };
        bound(vx, ry * vy, 0.0, s.len * s.len);
        bound(-vy, ry * vx, -rho * s.len, rho * s.len);
        if (a <= b) {
          lo = std::min(lo, s.a.x + a);
          hi = std::max(hi, s.a.x + b);
        }
      }
      if (!(lo <= hi)) return false;
      out = {blend::clampToInt(std::ceil(lo - 0.5), 0, w),
             blend::clampToInt(std::floor(hi - 0.5), -1, w - 1)};
      return out.first <= out.second;
    }

    // The inked segments, clipped: past `reach` outside the image no pixel can see them.
    std::vector<Segment> collect(const std::vector<Point>& pts, bool closed,
                                 const Pen& pen, int w, int h) {
      std::vector<Segment> out;
      const std::size_t n = closed ? pts.size() : pts.size() - 1;
      double arc = 0.0;
      for (std::size_t i = 0; i < n; ++i) {
        const Point& a = pts[i];
        const Point& b = pts[(i + 1) % pts.size()];
        const double vx = b.x - a.x, vy = b.y - a.y, len = std::sqrt(vx * vx + vy * vy);
        double t0 = 0.0, t1 = 0.0;
        if (clip(a, b, -pen.reach, w + pen.reach, h + pen.reach, t0, t1)) {
          Segment s{{a.x + t0 * vx, a.y + t0 * vy}, {a.x + t1 * vx, a.y + t1 * vy},
                    arc + t0 * len, (t1 - t0) * len};
          s.top = blend::clampToInt(std::floor(std::min(s.a.y, s.b.y) - pen.reach), 0, h - 1);
          s.bottom = blend::clampToInt(std::ceil(std::max(s.a.y, s.b.y) + pen.reach), 0, h - 1);
          if (nearestInk(s, pen.dash, 0.0) >= 0.0) out.push_back(s);
        }
        arc += len;
      }
      return out;
    }

    // Sorted, with touching spans joined.
    void mergeSpans(std::vector<Span>& spans) {
      std::sort(spans.begin(), spans.end());
      std::size_t k = 0;
      for (std::size_t i = 1; i < spans.size(); ++i) {
        if (spans[i].first <= spans[k].second + 1)
          spans[k].second = std::max(spans[k].second, spans[i].second);
        else
          spans[++k] = spans[i];
      }
      if (!spans.empty()) spans.resize(k + 1);
    }

    // The pass itself: `see(n)` per n pixels examined, `ink(x, y, cov)` per blend.
    template <class See, class Ink>
    void walk(int w, int h, const std::vector<Point>& pts, bool closed, double thickness,
              const DashPattern* dash, See see, Ink ink) {
      if (pts.size() < 2 || !(thickness > 0.0) || w <= 0 || h <= 0) return;
      const double radius = std::max(std::min(thickness, MAX_STROKE_THICKNESS) * 0.5, 0.5);
      const Pen pen{radius, radius - 0.5, radius + 0.5, dash};
      std::vector<Segment> segs = collect(pts, closed, pen, w, h);
      if (segs.empty()) return;
      std::sort(segs.begin(), segs.end(),
                [](const Segment& l, const Segment& r) { return l.top < r.top; });
      int last = 0;
      for (const Segment& s : segs) last = std::max(last, s.bottom);
      std::vector<double> cov(static_cast<std::size_t>(w), 0.0);  // this row's coverage
      std::vector<std::size_t> active;
      std::vector<Span> touched, core;
      const bool solidCore = dash == nullptr && pen.rIn > 0.0;
      std::size_t next = 0;
      for (int y = segs[0].top; y <= last; ++y) {
        if (active.empty() && next < segs.size()) y = std::max(y, segs[next].top);
        while (next < segs.size() && segs[next].top <= y) active.push_back(next++);
        active.erase(std::remove_if(active.begin(), active.end(),
                                    [&](std::size_t i) { return segs[i].bottom < y; }),
                     active.end());
        const double py = y + 0.5;
        touched.clear();
        core.clear();
        for (const std::size_t i : active) {
          Span out{0, -1}, in{0, -1};
          if (!rowSpan(segs[i], py, pen.reach, w, out)) continue;
          touched.push_back(out);
          bool full = solidCore && rowSpan(segs[i], py, pen.rIn, w, in);
          if (full) {
            in = {std::max(in.first, out.first), std::min(in.second, out.second)};
            full = in.first <= in.second;
          }
          if (full) core.push_back(in);
          for (int x = out.first; x <= out.second; ++x) {
            if (full && x == in.first) {  // the full-coverage run is filled once, below
              x = in.second;
              continue;
            }
            cov[x] = std::max(cov[x], coverageAt(segs[i], pen, x + 0.5, py));
            see(1);
          }
        }
        mergeSpans(core);
        for (const Span& sp : core)
          std::fill(cov.begin() + sp.first, cov.begin() + sp.second + 1, 1.0);
        mergeSpans(touched);
        for (const Span& sp : touched) {
          for (int x = sp.first; x <= sp.second; ++x) {
            if (cov[x] > 0.0) ink(x, y, cov[x]);
            cov[x] = 0.0;
          }
          see(static_cast<std::size_t>(sp.second - sp.first + 1));
        }
      }
    }

  }  // namespace

  void strokePolyline(std::uint8_t* buf, int w, int h, const std::vector<Point>& pts,
                      bool closed, double thickness, const DashPattern* dash, const Rgba& c) {
    walk(w, h, pts, closed, thickness, dash, [](std::size_t) {},
         [&](int x, int y, double cov) { blend::blendPixel(buf, w, h, x, y, c, cov); });
  }

  StrokeWork strokeWork(int w, int h, const std::vector<Point>& pts, bool closed,
                        double thickness, const DashPattern* dash) {
    StrokeWork work;
    walk(w, h, pts, closed, thickness, dash, [&](std::size_t n) { work.visits += n; },
         [&](int, int, double) { ++work.blends; });
    return work;
  }

}  // namespace stencil::core::coverage

#include "themeSwapOverlay.hpp"

namespace stencil::gui {


  // The easing the radius follows, and it is NOT a plain ease-out. The wipe is a CIRCLE,
  // so the area it has recoloured grows as r²: with OutCubic the circle had covered
  // ~95% of the window by 40% of the duration and the remaining 60% of the time went on
  // a sliver in the far corner, which is why the animation read as a snap followed by
  // nothing. Easing the radius IN slightly makes the AREA grow evenly, so the sweep uses
  // its whole duration and reads as durable. Same shape as the browser's
  // cubic-bezier(0.4, 0.25, 0.95, 1) — see the note over ::view-transition-new(root) in
  // browser/css/animations.css, which records the measurements this came from.
  // cubic-bezier(x1, y1, x2, y2) at t — the shared solver (dustKit.hpp), kept here by
  // name for the headless test and the grain's own curve below.
  double ThemeSwapOverlay::bezierY(double t, double x1, double y1, double x2, double y2) {
    return support::bezierY(t, x1, y1, x2, y2);
  }

  double ThemeSwapOverlay::grainEase(double t) {
    static const std::array<double, kGrainSteps + 1> curve = [] {
      std::array<double, kGrainSteps + 1> c{};
      for (int i = 0; i <= kGrainSteps; i++)
        c[i] = bezierY(double(i) / kGrainSteps, 0.22, 0.55, 0.3, 1.0);
      c.front() = 0.0;
      c.back() = 1.0;
      return c;
    }();
    return curve[std::clamp(int(std::lround(t * kGrainSteps)), 0, kGrainSteps)];
  }


  // Vertex k's reach off the nominal radius, as a share of it.
  double ThemeSwapOverlay::edgeJitter(support::ParticleStyle s, int k, int points) {
    const double t = double(k) / points;
    if (s == support::ParticleStyle::Water)
      return kWaterAmp * std::sin(kTau * kWaterWaves * t) + kWaterRippleAmp * std::sin(kTau * kWaterRipple * t + 1);
    if (s == support::ParticleStyle::Fire) {
      const double tongue = std::floor(t * kFireTongues), u = t * kFireTongues - tongue;
      const double h = kFireBase + kFireVary * dustNoise(int(tongue), 5);
      return h * std::pow(std::sin(kTau / 2 * u), 3) - kFireDip + kFireJag * (dustNoise(k, 7) * 2 - 1);
    }
    return 0.0;
  }


  // The deepest dip inward: the ring's base overshoots by it (plus slack) so the
  // finished front still clears the furthest corner, and the wake hugs just inside it.
  double ThemeSwapOverlay::edgeDipOf(support::ParticleStyle s) {
    if (s == support::ParticleStyle::Water) return kWaterAmp + kWaterRippleAmp;
    if (s == support::ParticleStyle::Fire) return kFireDip + kFireJag;
    return 0.0;
  }


  // Where vertex k of the front is at eased progress `e` (swapEase of the wipe time),
  // `full` being the corner-reaching radius. Pure — the headless test pins it to the
  // same coverage contract browser motion.test.js holds swapEdgePolygon to.
  double ThemeSwapOverlay::edgeRadiusAt(int k, double e, double full, support::ParticleStyle s) {
    return e * full * edgeBaseOf(s) * (1 + edgeJitter(s, k));
  }


  // Deterministic per-mote jitter — the shared scatter hash (browser tileNoise,
  // DisintegrateOverlay::cellNoise), so every surface's dust is cut from one cloth.
  double ThemeSwapOverlay::dustNoise(int a, int b) {
    const double h = std::sin(a * 127.1 + b * 311.7) * 43758.5453;
    return h - std::floor(h);
  }

  bool ThemeSwapOverlay::dustMoteAt(int i, double ms, const QPointF& origin, double full,
                                    const QSizeF& bounds, DustMote* out,
                                    support::ParticleStyle s) {
    const double n = dustNoise(i, 3), m = dustNoise(i + 57, 11), q = dustNoise(i + 13, 29);
    const double u = kDustMinT + m * (kDustMaxT - kDustMinT);
    const double life = (ms - u * kSwapMs) / kDustLifeMs;
    if (life <= 0.0 || life >= 1.0) return false;
    const double angle = n * kTau;
    // Hug the front: just behind even its deepest dip, so the band of grains and the
    // clip read as one crumbling front.
    const double r = swapEase(u) * full * (1 - edgeDipOf(s)) - q * 6;
    if (r <= 0) return false;
    const double hx = origin.x() + std::cos(angle) * r;
    const double hy = origin.y() + std::sin(angle) * r;
    if (hx < -16 || hy < -16 || hx > bounds.width() + 16 || hy > bounds.height() + 16) return false;
    // The browser's swapDustFrame, op for op: opacity flares over the first 18% of the
    // life and falls away across the rest, each leg on the grain's curve; the throw and
    // the shrink ride one pass of it.
    const double e = grainEase(life);
    const double o = life < kGrainFlare
                         ? grainEase(life / kGrainFlare)
                         : 1.0 - grainEase((life - kGrainFlare) / (1.0 - kGrainFlare));
    const double alpha = (0.75 + q * 0.25) * o;
    if (alpha < 1.0 / 255) return false;   // below one 8-bit step — nothing to paint
    // Chase the front outward, slower than it (the ring accelerates away), plus a
    // sideways breath so the wake churns instead of radiating.
    const double d = 8 + q * 14;
    const double dx = std::round(std::cos(angle) * d + (m - 0.5) * 14);
    const double dy = std::round(std::sin(angle) * d + (0.5 - q) * 14);
    out->x = hx + dx * e;
    out->y = hy + dy * e;
    out->alpha = alpha;
    out->size = (2.5 + n * 3.5) * (1.0 - 0.7 * e);
    out->accent = i % 4 == 0;
    out->life = life;
    out->w = dustNoise(i + 71, 13);
    out->len = std::hypot(dx, dy);
    out->heading = support::headingOf(dx, dy, false);
    return true;
  }


  // Arm the wake, in the palette the wipe is ERASING — call with the colours as they
  // stood BEFORE the restyle: the departing `accent` and its `shade`, the same two
  // colours every cloud wears (browser motion.js swapDustPaint palette).
  void ThemeSwapOverlay::seedDust(const QColor& accent, const QColor& shade, bool dark) {
    if (!accent.isValid()) return;
    dustAccent_ = accent;
    dustShade_ = shade.isValid() ? shade : accent;
    dustDark_ = dark;
    dust_ = true;
  }
}  // namespace stencil::gui

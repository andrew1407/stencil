// The theme wipe's easing (src/support/ThemeSwapOverlay.hpp swapEase) — the desktop half
// of the contract browser/tests/motion.test.js holds the browser and extension to.
//
// The wipe is a CIRCLE, so the area it has recoloured grows as r². A plain ease-out on the
// radius (this used to be OutCubic) covers almost the whole window in the first third of
// the duration and then spends the rest creeping over a sliver in the far corner — which
// reads as a snap followed by nothing, and is why the animation felt too quick. The curve
// is therefore judged on the AREA it sweeps, not on the radius it moves.
#include "ThemeSwapOverlay.hpp"

#include <QApplication>
#include <cmath>
#include <cstdio>
#include <string>

using stencil::gui::ThemeSwapOverlay;

#include "support/check.hpp"

// Fraction of a w×h window covered by a circle of radius `r` about (cx, cy), sampled.
static double covered(double r, double cx, double cy, double w, double h) {
  const int n = 90;
  int inside = 0;
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++) {
      const double x = (i + 0.5) * w / n, y = (j + 0.5) * h / n;
      if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) inside++;
    }
  return double(inside) / (n * n);
}

// What `start()` does: the radius reaches the furthest corner from the origin.
static double fullRadius(double cx, double cy, double w, double h) {
  return std::hypot(std::max(cx, w - cx), std::max(cy, h - cy));
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  // A curve at all: starts still, ends at the full radius, never runs backwards.
  check(std::abs(ThemeSwapOverlay::swapEase(0.0)) < 1e-6, "starts at zero");
  check(std::abs(ThemeSwapOverlay::swapEase(1.0) - 1.0) < 1e-6, "ends at the full radius");
  {
    bool monotonic = true;
    double prev = -1;
    for (int i = 0; i <= 100; i++) {
      const double v = ThemeSwapOverlay::swapEase(i / 100.0);
      monotonic = monotonic && v >= prev - 1e-9;
      prev = v;
    }
    check(monotonic, "never runs backwards");
  }

  // THE regression, at every origin the app uses: a toolbar icon near a corner (where the
  // theme button is) and the window centre (the fallback when no control is on screen).
  const double w = 1440, h = 900;
  const struct { const char* name; double cx, cy; } origins[] = {
    {"toolbar icon", 90, 60},
    {"window centre", w / 2, h / 2},
  };
  for (const auto& o : origins) {
    const double R = fullRadius(o.cx, o.cy, w, h);
    const auto at = [&](double t) { return covered(ThemeSwapOverlay::swapEase(t) * R, o.cx, o.cy, w, h); };
    // Nothing may be finished early, or the rest of the duration is dead time — the whole
    // point of the change.
    check(at(0.8) < 0.92, (std::string(o.name) + ": still visibly moving at 80% of the time").c_str());
    check(at(0.9) < 0.99, (std::string(o.name) + ": and at 90%").c_str());
    // …and the opposite trap: it must not creep at the start and then rush.
    check(at(0.4) > 0.15, (std::string(o.name) + ": away from the origin without creeping").c_str());
    // Even growth: no tenth of the duration may sweep more than a third of the window.
    bool even = true;
    double prev = 0;
    for (double t = 0.1; t <= 1.0001; t += 0.1) {
      const double c = at(std::min(t, 1.0));
      even = even && (c - prev) < 0.34 && c >= prev - 1e-9;
      prev = c;
    }
    check(even, (std::string(o.name) + ": no tenth floods a third of the window").c_str());
  }

  // The curve this replaced, kept as the proof the test would have caught it: OutCubic
  // pushed most of the window through in its first 40% and left the rest to a crawl.
  {
    const double R = fullRadius(90, 60, w, h);
    const auto outCubic = [](double t) { return 1 - std::pow(1 - t, 3); };
    const double oldAt40 = covered(outCubic(0.4) * R, 90, 60, w, h);
    const double newAt40 = covered(ThemeSwapOverlay::swapEase(0.4) * R, 90, 60, w, h);
    std::printf("       (OutCubic covers %.2f of the window by 40%%; this curve %.2f)\n", oldAt40, newAt40);
    check(oldAt40 - newAt40 > 0.25, "the old curve front-loaded the sweep far more than this one");
    check(covered(outCubic(0.8) * R, 90, 60, w, h) > 0.97,
          "…and had nothing left to show over its last fifth");
  }

  // ── The front (edgeRadiusAt — browser motion.test.js pins swapEdgePolygon to the same
  // contract). Its edge wears the particle style, and coverage still rules: at full
  // progress even the deepest dip must clear the furthest corner.
  {
    using stencil::support::ParticleStyle;
    const double R = fullRadius(90, 60, w, h);
    for (const ParticleStyle s : {ParticleStyle::DUST, ParticleStyle::WATER, ParticleStyle::FIRE}) {
      double lo = 1e18, hi = 0;
      bool collapsed = true;
      for (int k = 0; k < ThemeSwapOverlay::EDGE_POINTS; k++) {
        const double r = ThemeSwapOverlay::edgeRadiusAt(k, 1.0, R, s);
        lo = std::min(lo, r);
        hi = std::max(hi, r);
        collapsed = collapsed && ThemeSwapOverlay::edgeRadiusAt(k, 0.0, R, s) == 0.0;
      }
      check(lo >= R, "every vertex of the finished front clears the furthest corner");
      check(hi <= R * 1.15, "no tongue overshoots wildly");
      check(collapsed, "the front starts collapsed at the origin");
      if (s == ParticleStyle::DUST) check(hi - lo < 1e-9, "dust: a perfect circle");
      else check(hi - lo > R * 0.03, "water / fire: visibly not a circle");
    }
    // The browser's edgeJitter, op for op (values printed from node — dustCloud.js).
    const auto near = [](double a, double b) { return std::abs(a - b) < 1e-9; };
    check(near(ThemeSwapOverlay::edgeJitter(ParticleStyle::WATER, 17), -0.015235022) , "water vertex 17 matches the browser");
    check(near(ThemeSwapOverlay::edgeJitter(ParticleStyle::FIRE, 17), 0.043404486), "fire vertex 17 matches the browser");
    check(ThemeSwapOverlay::edgeJitter(ParticleStyle::DUST, 17) == 0.0, "dust vertex 17 is on the circle");
  }

  // ── Dust in the wipe's wake (dustMoteAt — browser motion.test.js pins swapDustSpecs
  // to the same contract). A mote ignites where the ring's edge has just passed, so at
  // any moment every visible mote must sit INSIDE the circle: the browser renders its
  // page through a clip to it, and a mote the desktop painted ahead of the front would
  // be one the browser could never show.
  {
    const QPointF o(90, 60);
    const QSizeF bounds(w, h);
    const double full = fullRadius(o.x(), o.y(), w, h);
    ThemeSwapOverlay::DustMote mote;
    bool none = true;
    for (int i = 0; i < ThemeSwapOverlay::DUST_MOTES; i++)
      none = none && !ThemeSwapOverlay::dustMoteAt(i, 0.0, o, full, bounds, &mote);
    check(none, "no mote before the wipe's first tick (the snapshot grab must stay clean)");

    bool inside = true, onScreen = true, sane = true, anyLate = false;
    int seen = 0;
    for (double ms = 20; ms <= ThemeSwapOverlay::SWAP_MS + ThemeSwapOverlay::DUST_LIFE_MS; ms += 20) {
      const double ring =
          full * ThemeSwapOverlay::swapEase(std::min(1.0, ms / ThemeSwapOverlay::SWAP_MS));
      for (int i = 0; i < ThemeSwapOverlay::DUST_MOTES; i++) {
        if (!ThemeSwapOverlay::dustMoteAt(i, ms, o, full, bounds, &mote)) continue;
        seen++;
        anyLate = anyLate || ms > ThemeSwapOverlay::SWAP_MS;
        inside = inside && std::hypot(mote.x - o.x(), mote.y - o.y()) <= ring + 1;
        // Homes are gated to ±16 of the screen; the drift can carry a mote ~29px
        // further before it fades, where the edge clips it — that slack is the bound.
        onScreen = onScreen && mote.x >= -48 && mote.y >= -48 && mote.x <= w + 48 && mote.y <= h + 48;
        sane = sane && mote.alpha > 0 && mote.alpha <= 1 && mote.size > 0 && mote.size <= 6;
      }
    }
    check(seen > 300, "a real field of motes across the wipe, not a sprinkle");
    check(inside, "every mote stays in the ring's wake — never ahead of the front");
    check(onScreen, "no mote is spent off screen");
    check(sane, "alpha and grain stay in range");
    check(anyLate, "the wake outlives the wipe — the last motes still get their whole life");
    // …but not forever: past the tail the wake is spent, so deleteLater leaves nothing.
    bool spent = true;
    const double after = ThemeSwapOverlay::SWAP_MS + ThemeSwapOverlay::DUST_LIFE_MS + 1;
    for (int i = 0; i < ThemeSwapOverlay::DUST_MOTES; i++)
      spent = spent && !ThemeSwapOverlay::dustMoteAt(i, after, o, full, bounds, &mote);
    check(spent, "every mote has burnt out by the overlay's own end");
    // Deterministic — a hash, not qrand: the same index at the same time is the same mote.
    ThemeSwapOverlay::DustMote a{}, b{};   // zero-init: the compare must hold even if not alive
    check(ThemeSwapOverlay::dustMoteAt(7, 150, o, full, bounds, &a)
              == ThemeSwapOverlay::dustMoteAt(7, 150, o, full, bounds, &b)
          && a.x == b.x && a.y == b.y && a.size == b.size && a.alpha == b.alpha,
          "the wake is deterministic");
  }

  // ── The grain's own curve, against the browser (motion.js swapDustEase /
  // swapDustFrame). The wake used to approximate the .swap-dust-mote keyframes with an
  // ease-out cubic and a linear opacity ramp; both surfaces now evaluate the real
  // cubic-bezier(0.22, 0.55, 0.3, 1), so these rows are the browser's own answers —
  // life, ease, throw (of 100px), radius (of a 6px grain) and opacity.
  {
    struct Row { double life, ease, x, r, alpha; };
    static constexpr Row BROWSER[] = {
      {0.000000000, 0.000000000, 0.000000000, 3.000000000, 0.000000000},
      {0.050000000, 0.130636662, 13.063666224, 2.725663009, 0.643748641},
      {0.180000000, 0.456230521, 45.623052120, 2.041915905, 1.000000000},
      {0.250000000, 0.597751975, 59.775197506, 1.744720852, 0.776672065},
      {0.400000000, 0.796361804, 79.636180401, 1.327640212, 0.368897378},
      {0.500000000, 0.877435088, 87.743508816, 1.157386315, 0.211374760},
      {0.750000000, 0.976566136, 97.656613588, 0.949211115, 0.036658227},
      {0.900000000, 0.996591210, 99.659121037, 0.907158458, 0.004921913},
      {0.990000000, 0.999957621, 99.995762110, 0.900088996, 0.000042379},
      {1.000000000, 1.000000000, 100.000000000, 0.900000000, 0.000000000},
    };
    // The browser tabulates its curve in float32, this one in double: they agree to far
    // more than a grain (or an 8-bit alpha) can show.
    constexpr double EPS = 1e-5;
    bool aligned = true;
    for (const Row& r : BROWSER) {
      const double e = ThemeSwapOverlay::grainEase(r.life);
      const double o = r.life < ThemeSwapOverlay::GRAIN_FLARE
                           ? ThemeSwapOverlay::grainEase(r.life / ThemeSwapOverlay::GRAIN_FLARE)
                           : 1.0 - ThemeSwapOverlay::grainEase(
                                       (r.life - ThemeSwapOverlay::GRAIN_FLARE)
                                       / (1.0 - ThemeSwapOverlay::GRAIN_FLARE));
      aligned = aligned && std::abs(e - r.ease) < EPS
                && std::abs(100.0 * e - r.x) < EPS            // the whole throw, eased
                && std::abs(3.0 * (1 - 0.7 * e) - r.r) < EPS  // scale(0.3) by the end
                && std::abs(o - r.alpha) < EPS;
    }
    check(aligned, "the grain rides the browser's curve, not an approximation of it");
    // …and the WAKE rides that curve, not just the helper: take a real mote at each of
    // those lives and read the ease back out of the size and the alpha it came with.
    {
      const QPointF org(90, 60);
      const double full3 = fullRadius(org.x(), org.y(), w, h);
      // Most indices are culled (off screen, or behind the ring) — take the first that
      // survives its whole flight, rather than pinning one the noise could move.
      const auto ignition = [](int i) {
        return ThemeSwapOverlay::DUST_MIN_T
               + ThemeSwapOverlay::dustNoise(i + 57, 11)
                     * (ThemeSwapOverlay::DUST_MAX_T - ThemeSwapOverlay::DUST_MIN_T);
      };
      const auto at = [&](int i, double life, ThemeSwapOverlay::DustMote* out) {
        return ThemeSwapOverlay::dustMoteAt(
            i, ignition(i) * ThemeSwapOverlay::SWAP_MS + life * ThemeSwapOverlay::DUST_LIFE_MS,
            org, full3, QSizeF(w, h), out);
      };
      int i = -1;
      ThemeSwapOverlay::DustMote mo{};
      for (int c = 0; c < ThemeSwapOverlay::DUST_MOTES && i < 0; c++) {
        bool whole = true;
        for (const Row& r : BROWSER)
          if (r.life > 0 && r.alpha >= 1.0 / 255) whole = whole && at(c, r.life, &mo);
        if (whole) i = c;
      }
      const double base = 2.5 + ThemeSwapOverlay::dustNoise(i, 3) * 3.5;
      const double lit = 0.75 + ThemeSwapOverlay::dustNoise(i + 13, 29) * 0.25;
      bool rides = i >= 0;
      int sampled = 0;
      for (const Row& r : BROWSER) {
        if (r.life <= 0 || r.alpha < 1.0 / 255) continue;   // not alive, or too faint to paint
        if (!at(i, r.life, &mo)) continue;
        sampled++;
        // Read the ease back out of what the mote came with: size is base * (1 − 0.7e),
        // alpha is its own brightness times the flare.
        rides = rides && std::abs((1.0 - mo.size / base) / 0.7 - r.ease) < 1e-4
                && std::abs(mo.alpha / lit - r.alpha) < 1e-4;
      }
      check(sampled >= 5 && rides, "a mote's own size and alpha carry the browser's curve");
    }
    // Ends exactly: a grain starts home and unlit, and leaves nothing behind.
    check(ThemeSwapOverlay::grainEase(0.0) == 0.0 && ThemeSwapOverlay::grainEase(1.0) == 1.0,
          "both ends of the curve are pinned");
    // An invisible grain is not a grain: below one 8-bit alpha step it declines to paint.
    ThemeSwapOverlay::DustMote faint{};
    const QPointF o2(90, 60);
    const double full2 = fullRadius(o2.x(), o2.y(), w, h);
    bool anyInvisible = false;
    for (double ms = 1; ms <= ThemeSwapOverlay::SWAP_MS + ThemeSwapOverlay::DUST_LIFE_MS; ms += 1)
      for (int i = 0; i < 400; i++)
        if (ThemeSwapOverlay::dustMoteAt(i, ms, o2, full2, QSizeF(w, h), &faint))
          anyInvisible = anyInvisible || faint.alpha < 1.0 / 255;
    check(!anyInvisible, "no grain is handed back with nothing to show");
  }

  std::printf("%s\n", failures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILURES");
  return failures == 0 ? 0 : 1;
}

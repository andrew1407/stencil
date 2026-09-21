// The theme wipe's easing (src/support/ThemeSwapOverlay.hpp swapEase) — the desktop half of the
// contract browser/tests/motion.test.js holds the browser and extension to. The wipe is a CIRCLE, so
// the area it has recoloured grows as r², and the curve is judged on the AREA it sweeps, not the
// radius it moves. Split across themeSwapEase*.headless.cpp.
#include "themeSwapEaseParts.hpp"

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

  // The front (edgeRadiusAt — browser motion.test.js pins swapEdgePolygon to the same contract): its edge
  // wears the particle style, and coverage rules — at full progress the deepest dip clears the corner.
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
    // The browser's edgeJitter, op for op (values printed from node — dust/cloud.js).
    const auto near = [](double a, double b) { return std::abs(a - b) < 1e-9; };
    check(near(ThemeSwapOverlay::edgeJitter(ParticleStyle::WATER, 17), -0.015235022) , "water vertex 17 matches the browser");
    check(near(ThemeSwapOverlay::edgeJitter(ParticleStyle::FIRE, 17), 0.043404486), "fire vertex 17 matches the browser");
    check(ThemeSwapOverlay::edgeJitter(ParticleStyle::DUST, 17) == 0.0, "dust vertex 17 is on the circle");
  }

  // Dust in the wipe's wake (dustMoteAt — browser swapDustSpecs): a mote ignites where the ring's edge
  // has just passed, so every visible mote sits INSIDE the circle the browser clips its page to.
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

  // The grain's own curve, against the browser (surface/motion.js swapDustEase / swapDustFrame): both surfaces
  // evaluate the real cubic-bezier(0.22, 0.55, 0.3, 1), so these rows are the browser's own answers.
  dustGrainCurve(w, h);

  std::printf("%s\n", failures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILURES");
  return failures == 0 ? 0 : 1;
}

// The theme wipe's easing (src/support/themeSwapOverlay.hpp swapEase) — the desktop half
// of the contract browser/tests/motion.test.js holds the browser and extension to.
//
// The wipe is a CIRCLE, so the area it has recoloured grows as r². A plain ease-out on the
// radius (this used to be OutCubic) covers almost the whole window in the first third of
// the duration and then spends the rest creeping over a sliver in the far corner — which
// reads as a snap followed by nothing, and is why the animation felt too quick. The curve
// is therefore judged on the AREA it sweeps, not on the radius it moves.
#include "themeSwapOverlay.hpp"

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

  // ── The ragged front (edgeRadiusAt — browser motion.test.js pins swapEdgePolygon to
  // the same contract). The hole's edge is torn, not a circle line — but coverage still
  // rules: at full progress even the deepest tooth must clear the furthest corner.
  {
    const double R = fullRadius(90, 60, w, h);
    double lo = 1e18, hi = 0;
    bool collapsed = true;
    for (int k = 0; k < ThemeSwapOverlay::kEdgePoints; k++) {
      const double r = ThemeSwapOverlay::edgeRadiusAt(k, 1.0, R);
      lo = std::min(lo, r);
      hi = std::max(hi, r);
      collapsed = collapsed && ThemeSwapOverlay::edgeRadiusAt(k, 0.0, R) == 0.0;
    }
    check(lo >= R, "every tooth of the finished front clears the furthest corner");
    check(hi <= R * (1 + 2 * ThemeSwapOverlay::kEdgeAmp + 0.02), "no tooth overshoots wildly");
    check(hi - lo > R * ThemeSwapOverlay::kEdgeAmp, "ragged, not a circle in disguise");
    check(collapsed, "the front starts collapsed at the origin");
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
    for (int i = 0; i < ThemeSwapOverlay::kDustMotes; i++)
      none = none && !ThemeSwapOverlay::dustMoteAt(i, 0.0, o, full, bounds, &mote);
    check(none, "no mote before the wipe's first tick (the snapshot grab must stay clean)");

    bool inside = true, onScreen = true, sane = true, anyLate = false;
    int seen = 0;
    for (double ms = 20; ms <= ThemeSwapOverlay::kSwapMs + ThemeSwapOverlay::kDustLifeMs; ms += 20) {
      const double ring =
          full * ThemeSwapOverlay::swapEase(std::min(1.0, ms / ThemeSwapOverlay::kSwapMs));
      for (int i = 0; i < ThemeSwapOverlay::kDustMotes; i++) {
        if (!ThemeSwapOverlay::dustMoteAt(i, ms, o, full, bounds, &mote)) continue;
        seen++;
        anyLate = anyLate || ms > ThemeSwapOverlay::kSwapMs;
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
    const double after = ThemeSwapOverlay::kSwapMs + ThemeSwapOverlay::kDustLifeMs + 1;
    for (int i = 0; i < ThemeSwapOverlay::kDustMotes; i++)
      spent = spent && !ThemeSwapOverlay::dustMoteAt(i, after, o, full, bounds, &mote);
    check(spent, "every mote has burnt out by the overlay's own end");
    // Deterministic — a hash, not qrand: the same index at the same time is the same mote.
    ThemeSwapOverlay::DustMote a{}, b{};   // zero-init: the compare must hold even if not alive
    check(ThemeSwapOverlay::dustMoteAt(7, 150, o, full, bounds, &a)
              == ThemeSwapOverlay::dustMoteAt(7, 150, o, full, bounds, &b)
          && a.x == b.x && a.y == b.y && a.size == b.size && a.alpha == b.alpha,
          "the wake is deterministic");
  }

  std::printf("%s\n", failures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILURES");
  return failures == 0 ? 0 : 1;
}

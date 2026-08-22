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

  std::printf("%s\n", failures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILURES");
  return failures == 0 ? 0 : 1;
}

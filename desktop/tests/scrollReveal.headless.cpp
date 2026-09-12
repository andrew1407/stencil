// Headless check of the scroll-reveal curve (src/support/scrollReveal.hpp) — the desktop
// port of the browser's .reveal-item / .reveal-in rules (browser/css/animations.css,
// driven by browser/js/ui/motion.js). The widget/delegate plumbing needs a live view,
// but the curve that decides how dim a row is does not, so that is what is pinned here:
// full opacity clear of both bands, a ramp inside them, the rest state off-screen, and
// the deliberate asymmetry that keeps a transcript's newest (bottom-flush) row readable.
// Pure QtCore geometry; no display needed.
#include "scrollReveal.hpp"

#include <QCoreApplication>
#include <cmath>
#include <cstdio>

using stencil::gui::REVEAL_BOTTOM_BAND_PX;
using stencil::gui::REVEAL_MIN_OPACITY;
using stencil::gui::REVEAL_TOP_BAND_PX;
using stencil::gui::revealOpacity;
using stencil::gui::revealOpacityForItem;

#include "support/check.hpp"
static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const int viewH = 400;

  // ── Clear of both bands → untouched.
  check(near(revealOpacity(100, 200, viewH), 1.0), "an item in the middle is fully opaque");
  check(near(revealOpacity(REVEAL_TOP_BAND_PX, viewH - REVEAL_BOTTOM_BAND_PX, viewH), 1.0),
        "an item exactly spanning the inner box is fully opaque");

  // ── Off-screen → the rest state, in both directions.
  check(near(revealOpacity(-120, -20, viewH), REVEAL_MIN_OPACITY), "scrolled off the top → rest state");
  check(near(revealOpacity(viewH + 10, viewH + 90, viewH), REVEAL_MIN_OPACITY),
        "not yet scrolled in → rest state");

  // ── Inside a band → a ramp between the two, monotonic as the item climbs.
  const double half = revealOpacity(REVEAL_TOP_BAND_PX / 2, REVEAL_TOP_BAND_PX / 2 + 40, viewH);
  check(half > REVEAL_MIN_OPACITY && half < 1.0, "an item halfway into the top band is partly dim");
  check(revealOpacity(4, 60, viewH) < revealOpacity(40, 96, viewH),
        "the further into the top band, the dimmer");
  check(near(revealOpacity(0, 56, viewH), REVEAL_MIN_OPACITY),
        "an item flush with the top edge is at the rest state");

  // ── The asymmetry that matters: the chat transcript pins its NEWEST card flush to
  // the bottom edge, and that card must stay readable. A 40px row there keeps most of
  // its opacity, while the same row flush to the TOP is fully dimmed.
  const double newestCard = revealOpacity(viewH - 40, viewH, viewH);
  check(newestCard > 0.6, "a bottom-flush row (the newest chat card) stays readable");
  check(newestCard > revealOpacity(0, 40, viewH), "the bottom band is gentler than the top band");

  // ── Degenerate inputs never dim: a view with no height, or an empty rect.
  check(near(revealOpacity(0, 40, 0), 1.0), "a zero-height viewport means no fade, not a guess");
  check(near(revealOpacity(50, 50, viewH), 1.0), "an empty rect is not dimmed");

  // ── The item-view wrapper: a null viewport is the zero-height case (no fade), and a
  // delegate rect (inclusive bottom) maps onto the same curve.
  check(near(revealOpacityForItem(nullptr, QRect(0, 0, 100, 40)), 1.0),
        "a delegate with no viewport paints at full opacity");

  // ── The dissolve mapping (support/dissolveEffect.hpp is driven by this) ──
  // The reveal ramp bottoms out at REVEAL_MIN_OPACITY, not 0, so it has to be RESCALED:
  // an out-of-view row must reach a FULL dissolve, not stop 18% short of one.
  using stencil::gui::ScrollReveal;
  check(near(ScrollReveal::dissolveFor(1.0), 0.0), "a fully revealed row is not dissolved at all");
  check(near(ScrollReveal::dissolveFor(REVEAL_MIN_OPACITY), 1.0), "an out-of-view row dissolves completely");
  {
    const double mid = ScrollReveal::dissolveFor((1.0 + REVEAL_MIN_OPACITY) / 2.0);
    check(mid > 0.45 && mid < 0.55, "the midpoint of the ramp is the midpoint of the dissolve");
  }
  check(near(ScrollReveal::dissolveFor(2.0), 0.0) && near(ScrollReveal::dissolveFor(-1.0), 1.0),
        "out-of-range input is clamped, never extrapolated");
  // Monotonic: more revealed is always less dissolved.
  check(ScrollReveal::dissolveFor(0.9) < ScrollReveal::dissolveFor(0.5),
        "dissolve falls as the row reveals");

  // ── Dissolve tracks ONLY the clipped share ──
  // Not a "band" inside the visible area: dissolving what you can still read turns a
  // message into an unreadable dot screen, because the grain is finer than a glyph's
  // strokes. Decoration must never cost legibility.
  using stencil::gui::revealDissolve;
  check(near(revealDissolve(0, 60, 495), 0.0), "a row flush with the top edge is untouched");
  check(near(revealDissolve(200, 240, 495), 0.0), "a row mid-scroller is untouched");
  check(near(revealDissolve(445, 495, 495), 0.0), "a row flush with the bottom edge is untouched");
  check(near(revealDissolve(-50, 50, 495), 0.5), "half cut off the top = half dissolved");

  // GRAIN vs the clipped share: a card TALLER than the viewport is clipped by
  // definition, so measuring the clipped share left it permanently speckled — grain
  // bands sat across a tall variant card's picture however you scrolled. The grain
  // measures against what the viewport can hold instead (browser motion.js revealGrain).
  using stencil::gui::revealGrain;
  check(near(revealGrain(-100, 100, 400), 0.5), "half off the top is half grainy either way");
  check(near(revealDissolve(-100, 100, 400), 0.5), "…and the clipped share agrees while it fits");
  check(near(revealGrain(0, 900, 300), 0.0), "a card taller than the viewport is NOT grainy");
  check(revealDissolve(0, 900, 300) > 0.6, "…even though most of it is clipped");
  check(near(revealGrain(0, 300, 300), 0.0), "an exactly-filling card is whole");
  check(near(revealGrain(400, 500, 300), 1.0), "a card fully below the fold is fully grainy");
  check(near(revealDissolve(445, 545, 495), 0.5), "half cut off the bottom = half dissolved");
  check(near(revealDissolve(-300, -260, 495), 1.0), "gone entirely once off screen");
  check(near(revealDissolve(0, 50, 0), 0.0), "a zero-height viewport is not a guess");
  check(near(revealDissolve(50, 50, 495), 0.0), "an empty row");

  std::printf("%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}

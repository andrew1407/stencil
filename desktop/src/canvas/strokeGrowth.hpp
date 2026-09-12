#pragma once
// Drawing a stroke: the desktop port of the "vertex in flight" section of
// browser/js/ui/motion.js + js/core/strokeFx.js — keep the two in step.
//
// Every route that adds a point ends in one motion: the new vertex leaves where it came
// from (the point it extends, or its foot on the segment it splits) and travels to the
// click on a bowed path, overshooting before it settles. The painter reads the flown
// position, so the segments hanging off it bend and join by themselves.
//
// A flight is keyed by (line index, target coordinates), not a pointer or a point index:
// inserting a point splices core::Line::points and invalidates both. Header-only and
// Q_OBJECT-free, so it needs no MOC.
#include <QPointF>

#include <algorithm>
#include <cmath>
#include <vector>

#include "disintegrateOverlay.hpp"   // cellNoise: the app's one scatter hash
#include "models.hpp"

namespace stencil::gui::stroke {

  // The flight's own length: a short hop is nearly instant, a reach across the page
  // still lands promptly. Lengths are IMAGE pixels on both sides, so zoom does not
  // change the timing (browser motion.js strokeFlyMs).
  constexpr double kFlyMinMs = 150.0;
  constexpr double kFlyMaxMs = 420.0;
  constexpr double kFlyPxPerMs = 2.4;
  double flyMs(double len);

  // Ease-out-back: the vertex shoots a little past its target and comes back, which is
  // what makes the segment read as REACHING for the point rather than being switched
  // on. Weaker than the textbook 1.70158 — on a 3px stroke a big overshoot is a glitch.
  constexpr double kFlyBack = 1.28;
  double flyEase(double t);

  // No vertex flies a straight line (the rule the dust follows too — tileWaypoint): it
  // is pushed off its path by a capped share of the trip, and back by the time it lands.
  constexpr double kBowShare = 0.13;
  constexpr double kBowMax = 22.0;
  inline double bowAmp(double len) { return std::min(len * kBowShare, kBowMax); }

  double bowSign(double x, double y);

  double arc(double t);

  QPointF flyPoint(const QPointF& from, const QPointF& to, double t, double bow);

  // The landing: the vertex arrives half again its size and settles. The swell happens
  // in flight (flyRadius), so there is no jump between the two — a size that snaps on
  // arrival reads as a redraw, not a landing.
  constexpr double kPopMs = 240.0;
  constexpr double kPopPeak = 1.5;
  constexpr double kFlyR0 = 0.5;
  double flyRadius(double t);
  double popScale(double u);

  // The ring the landing pushes out — the one part of this that is not the line itself,
  // so it stays faint and brief.
  constexpr double kRippleMs = 420.0;
  constexpr double kRippleReach = 4.2;
  constexpr double kRippleAlpha = 0.55;
  struct Ring { double scale; double alpha; };
  Ring ripple(double u);

  // The glow riding the vertex in flight: nothing at either end (it must not smudge the
  // anchor it left or the point it became), brightest mid-trip.
  constexpr double kSparkReach = 2.8;
  constexpr double kSparkAlpha = 0.6;
  Ring spark(double t);

  // How hot the segments the vertex is dragging burn, over the whole flight + settle:
  // full as it leaves, out by the time it has landed.
  constexpr double kWakeAlpha = 0.5;
  double wake(double t);

  // The whole timeline of one vertex. `land` drives the settle, `ripple` the ring; both
  // start the moment the flight ends.
  struct Phase {
    double fly = 1.0;
    double land = 1.0;
    double ripple = 1.0;
    double span = 1.0;
    bool done = true;
  };
  Phase phase(double elapsed, double fly);
  double vertexScale(const Phase& ph);

  QPointF foot(const core::Point& a, const core::Point& b, double x, double y);

  // The flights themselves (browser js/core/strokeFx.js)
  struct Flight {
    int lineIdx = -1;      // index into lines_, or -1 for the in-progress line
    QPointF to;            // the vertex's resting place — also its identity
    QPointF from;
    double bow = 0.0;
    double fly = kFlyMinMs;
    double start = 0.0;    // ms on the widget's clock; may be in the FUTURE (staggered)
  };

  class Fx {
   public:
    bool active() const { return !flights_.empty(); }
    void clear() { flights_.clear(); }

    void flyIn(int lineIdx, const core::Line& line, int ptIdx, double now,
               const QPointF* from = nullptr);

    void flyInRange(int lineIdx, const core::Line& line, int startIdx, int count, double now);

    void rekey(int fromIdx, int toIdx);

    bool step(double now);

    const Flight* at(int lineIdx, const core::Point& p) const;

    bool touches(int lineIdx) const;

   private:
    void drop(int lineIdx, const QPointF& to);

    std::vector<Flight> flights_;
  };

}  // namespace stencil::gui::stroke

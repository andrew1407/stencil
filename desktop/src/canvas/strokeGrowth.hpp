#pragma once
// Drawing a stroke: port of browser/js/ui/motion.js "vertex in flight" + js/core/strokeFx.js.
// A flight is keyed by (line index, target coordinates): inserting a point splices
// core::Line::points, invalidating any pointer or index. Header-only, no MOC.
#include <QPointF>

#include <algorithm>
#include <cmath>
#include <vector>

#include "disintegrateOverlay.hpp"   // cellNoise: the app's one scatter hash
#include "models.hpp"

namespace stencil::gui::stroke {

  // Lengths are IMAGE px on both sides, so zoom does not change the timing (motion.js strokeFlyMs).
  constexpr double kFlyMinMs = 150.0;
  constexpr double kFlyMaxMs = 420.0;
  constexpr double kFlyPxPerMs = 2.4;
  double flyMs(double len);

  // Ease-out-back, weaker than the textbook 1.70158: on a 3px stroke a big overshoot is a glitch.
  constexpr double kFlyBack = 1.28;
  double flyEase(double t);

  // Pushed off a straight path by a capped share of the trip, back by landing (the dust's tileWaypoint rule).
  constexpr double kBowShare = 0.13;
  constexpr double kBowMax = 22.0;
  inline double bowAmp(double len) { return std::min(len * kBowShare, kBowMax); }

  double bowSign(double x, double y);

  double arc(double t);

  QPointF flyPoint(const QPointF& from, const QPointF& to, double t, double bow);

  // Arrives half again its size and settles; the swell happens in flight so nothing snaps on arrival.
  constexpr double kPopMs = 240.0;
  constexpr double kPopPeak = 1.5;
  constexpr double kFlyR0 = 0.5;
  double flyRadius(double t);
  double popScale(double u);

  // The landing ring: faint and brief.
  constexpr double kRippleMs = 420.0;
  constexpr double kRippleReach = 4.2;
  constexpr double kRippleAlpha = 0.55;
  struct Ring { double scale; double alpha; };
  Ring ripple(double u);

  // Nothing at either end (must not smudge the anchor or the landed point), brightest mid-trip.
  constexpr double kSparkReach = 2.8;
  constexpr double kSparkAlpha = 0.6;
  Ring spark(double t);

  // Segment heat: full as it leaves, out by landing.
  constexpr double kWakeAlpha = 0.5;
  double wake(double t);

  // `land` drives the settle, `ripple` the ring; both start when the flight ends.
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

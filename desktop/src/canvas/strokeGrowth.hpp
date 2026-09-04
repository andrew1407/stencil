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
  inline double flyMs(double len) {
    return std::min(kFlyMaxMs, kFlyMinMs + std::max(0.0, len) / kFlyPxPerMs);
  }

  // Ease-out-back: the vertex shoots a little past its target and comes back, which is
  // what makes the segment read as REACHING for the point rather than being switched
  // on. Weaker than the textbook 1.70158 — on a 3px stroke a big overshoot is a glitch.
  constexpr double kFlyBack = 1.28;
  inline double flyEase(double t) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    const double u = t - 1.0;
    return 1.0 + (kFlyBack + 1.0) * u * u * u + kFlyBack * u * u;
  }

  // No vertex flies a straight line (the rule the dust follows too — tileWaypoint): it
  // is pushed off its path by a capped share of the trip, and back by the time it lands.
  constexpr double kBowShare = 0.13;
  constexpr double kBowMax = 22.0;
  inline double bowAmp(double len) { return std::min(len * kBowShare, kBowMax); }

  // Which side, and how far off, THIS vertex swings — the app's ONE scatter hash, the
  // same one the motes use (browser motion.js strokeBowSign over tileNoise).
  inline double bowSign(double x, double y) {
    return (DisintegrateOverlay::cellNoise(static_cast<int>(std::lround(x)),
                                           static_cast<int>(std::lround(y))) - 0.5) * 2.0;
  }

  // The envelope every mid-flight flourish rides: nothing at either end, everything at the
  // half-way mark. A parabola, not a sine, because it is exactly zero at both ends —
  // sin(pi) is not (browser motion.js strokeArc).
  inline double arc(double t) {
    const double k = std::clamp(t, 0.0, 1.0);
    return 4.0 * k * (1.0 - k);
  }

  // Where the vertex is `t` through its flight.
  inline QPointF flyPoint(const QPointF& from, const QPointF& to, double t, double bow) {
    const double k = flyEase(t);
    const double dx = to.x() - from.x();
    const double dy = to.y() - from.y();
    const QPointF at(from.x() + dx * k, from.y() + dy * k);
    const double len = std::hypot(dx, dy);
    if (bow == 0.0 || len < 0.5) return at;
    const double s = arc(t) * bowAmp(len) * bow;
    return QPointF(at.x() - (dy / len) * s, at.y() + (dx / len) * s);
  }

  // The landing: the vertex arrives half again its size and settles. The swell happens
  // in flight (flyRadius), so there is no jump between the two — a size that snaps on
  // arrival reads as a redraw, not a landing.
  constexpr double kPopMs = 240.0;
  constexpr double kPopPeak = 1.5;
  constexpr double kFlyR0 = 0.5;
  inline double flyRadius(double t) {
    const double k = std::clamp(t, 0.0, 1.0);
    return kFlyR0 + (kPopPeak - kFlyR0) * k * k;
  }
  inline double popScale(double u) {
    if (u >= 1.0) return 1.0;
    const double k = 1.0 - std::clamp(u, 0.0, 1.0);
    return 1.0 + (kPopPeak - 1.0) * k * k;
  }

  // The ring the landing pushes out — the one part of this that is not the line itself,
  // so it stays faint and brief.
  constexpr double kRippleMs = 420.0;
  constexpr double kRippleReach = 4.2;
  constexpr double kRippleAlpha = 0.55;
  struct Ring { double scale; double alpha; };
  inline Ring ripple(double u) {
    const double k = std::clamp(u, 0.0, 1.0);
    return {1.0 + (kRippleReach - 1.0) * (1.0 - (1.0 - k) * (1.0 - k)),
            kRippleAlpha * std::pow(1.0 - k, 1.6)};
  }

  // The glow riding the vertex in flight: nothing at either end (it must not smudge the
  // anchor it left or the point it became), brightest mid-trip.
  constexpr double kSparkReach = 2.8;
  constexpr double kSparkAlpha = 0.6;
  inline Ring spark(double t) {
    const double k = arc(t);
    return {1.0 + (kSparkReach - 1.0) * k, kSparkAlpha * std::pow(k, 0.7)};
  }

  // How hot the segments the vertex is dragging burn, over the whole flight + settle:
  // full as it leaves, out by the time it has landed.
  constexpr double kWakeAlpha = 0.5;
  inline double wake(double t) {
    return kWakeAlpha * std::pow(1.0 - std::clamp(t, 0.0, 1.0), 1.3);
  }

  // The whole timeline of one vertex. `land` drives the settle, `ripple` the ring; both
  // start the moment the flight ends.
  struct Phase {
    double fly = 1.0;
    double land = 1.0;
    double ripple = 1.0;
    double span = 1.0;
    bool done = true;
  };
  inline Phase phase(double elapsed, double fly) {
    Phase ph;
    ph.fly = fly > 0.0 ? std::clamp(elapsed / fly, 0.0, 1.0) : 1.0;
    const double after = std::max(0.0, elapsed - fly);
    ph.land = std::min(1.0, after / kPopMs);
    ph.ripple = std::min(1.0, after / kRippleMs);
    ph.span = std::min(1.0, elapsed / (fly + kRippleMs));
    ph.done = elapsed >= fly + kRippleMs;
    return ph;
  }
  inline double vertexScale(const Phase& ph) {
    return ph.fly < 1.0 ? flyRadius(ph.fly) : popScale(ph.land);
  }

  // Where a vertex INSERTED into a segment comes from: its own foot on the straight line
  // it split, clamped to the segment, so the bend is pulled out of the stroke instead of
  // appearing beside it (browser motion.js strokeFoot).
  inline QPointF foot(const core::Point& a, const core::Point& b, double x, double y) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lenSq = dx * dx + dy * dy;
    if (lenSq == 0.0) return QPointF(a.x, a.y);
    const double t = std::clamp(((x - a.x) * dx + (y - a.y) * dy) / lenSq, 0.0, 1.0);
    return QPointF(a.x + t * dx, a.y + t * dy);
  }

  // ── The flights themselves (browser js/core/strokeFx.js) ──────────────────
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

    // Send the point at `ptIdx` on its way. `from` defaults to the neighbour it hangs off
    // (the one before, else the one after); a line's first point has none and only pops.
    void flyIn(int lineIdx, const core::Line& line, int ptIdx, double now,
               const QPointF* from = nullptr) {
      const int n = static_cast<int>(line.points.size());
      if (ptIdx < 0 || ptIdx >= n) return;
      const core::Point& pt = line.points[ptIdx];
      const QPointF to(pt.x, pt.y);
      QPointF src = to;
      if (from) src = *from;
      else if (ptIdx > 0) src = QPointF(line.points[ptIdx - 1].x, line.points[ptIdx - 1].y);
      else if (n > 1) src = QPointF(line.points[1].x, line.points[1].y);
      // The same vertex sent again replaces its own flight rather than stacking two
      // clocks on one point.
      drop(lineIdx, to);
      Flight f;
      f.lineIdx = lineIdx;
      f.to = to;
      f.from = src;
      f.bow = bowSign(to.x(), to.y());
      f.fly = flyMs(std::hypot(to.x() - src.x(), to.y() - src.y()));
      f.start = now;
      flights_.push_back(f);
    }

    // Several points at once (a rect's corners): each leaves the one before it and waits
    // for it to land, so the shape draws itself edge by edge. A range starting at the head
    // has nothing before it, so that first vertex only pops.
    void flyInRange(int lineIdx, const core::Line& line, int startIdx, int count, double now) {
      double delay = 0.0;
      for (int i = 0; i < count; ++i) {
        const int idx = startIdx + i;
        if (idx < 0 || idx >= static_cast<int>(line.points.size())) continue;
        const QPointF self(line.points[idx].x, line.points[idx].y);
        const size_t before = flights_.size();
        flyIn(lineIdx, line, idx, now + delay, idx == 0 ? &self : nullptr);
        if (flights_.size() > before) delay += flights_.back().fly * 0.55;
      }
    }

    // Follow a line to its new number — committing the in-progress line moves it into
    // lines_, and its vertices must keep flying on the line they became.
    void rekey(int fromIdx, int toIdx) {
      for (Flight& f : flights_)
        if (f.lineIdx == fromIdx) f.lineIdx = toIdx;
    }

    // Drop everything that has landed; true while anything is still in the air.
    bool step(double now) {
      flights_.erase(std::remove_if(flights_.begin(), flights_.end(),
                                    [&](const Flight& f) { return phase(now - f.start, f.fly).done; }),
                     flights_.end());
      return active();
    }

    // The flight a point is on, if any — matched on its resting coordinates.
    const Flight* at(int lineIdx, const core::Point& p) const {
      for (const Flight& f : flights_) {
        if (f.lineIdx == lineIdx && f.to.x() == p.x && f.to.y() == p.y) return &f;
      }
      return nullptr;
    }

    bool touches(int lineIdx) const {
      for (const Flight& f : flights_)
        if (f.lineIdx == lineIdx) return true;
      return false;
    }

   private:
    void drop(int lineIdx, const QPointF& to) {
      flights_.erase(std::remove_if(flights_.begin(), flights_.end(),
                                    [&](const Flight& f) {
                                      return f.lineIdx == lineIdx && f.to == to;
                                    }),
                     flights_.end());
    }

    std::vector<Flight> flights_;
  };

}  // namespace stencil::gui::stroke

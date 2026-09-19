#include "strokeGrowth.hpp"

namespace stencil::gui::stroke {

  double flyMs(double len) {
    return std::min(FLY_MAX_MS, FLY_MIN_MS + std::max(0.0, len) / FLY_PX_PER_MS);
  }

  double flyEase(double t) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    const double u = t - 1.0;
    return 1.0 + (FLY_BACK + 1.0) * u * u * u + FLY_BACK * u * u;
  }

  // Which side, and how far off, THIS vertex swings — the app's ONE scatter hash, the
  // same one the motes use (browser motion.js strokeBowSign over tileNoise).
  double bowSign(double x, double y) {
    return (DisintegrateOverlay::cellNoise(static_cast<int>(std::lround(x)),
                                           static_cast<int>(std::lround(y))) - 0.5) * 2.0;
  }

  // The envelope every mid-flight flourish rides. A parabola, not a sine, because it is exactly
  // zero at both ends - sin(pi) is not (browser motion.js strokeArc).
  double arc(double t) {
    const double k = std::clamp(t, 0.0, 1.0);
    return 4.0 * k * (1.0 - k);
  }

  // Where the vertex is `t` through its flight.
  QPointF flyPoint(const QPointF& from, const QPointF& to, double t, double bow) {
    const double k = flyEase(t);
    const double dx = to.x() - from.x();
    const double dy = to.y() - from.y();
    const QPointF at(from.x() + dx * k, from.y() + dy * k);
    const double len = std::hypot(dx, dy);
    if (bow == 0.0 || len < 0.5) return at;
    const double s = arc(t) * bowAmp(len) * bow;
    return QPointF(at.x() - (dy / len) * s, at.y() + (dx / len) * s);
  }

  double flyRadius(double t) {
    const double k = std::clamp(t, 0.0, 1.0);
    return FLY_R0 + (POP_PEAK - FLY_R0) * k * k;
  }

  double popScale(double u) {
    if (u >= 1.0) return 1.0;
    const double k = 1.0 - std::clamp(u, 0.0, 1.0);
    return 1.0 + (POP_PEAK - 1.0) * k * k;
  }

  Ring ripple(double u) {
    const double k = std::clamp(u, 0.0, 1.0);
    return {1.0 + (RIPPLE_REACH - 1.0) * (1.0 - (1.0 - k) * (1.0 - k)),
            RIPPLE_ALPHA * std::pow(1.0 - k, 1.6)};
  }

  Ring spark(double t) {
    const double k = arc(t);
    return {1.0 + (SPARK_REACH - 1.0) * k, SPARK_ALPHA * std::pow(k, 0.7)};
  }

  double wake(double t) {
    return WAKE_ALPHA * std::pow(1.0 - std::clamp(t, 0.0, 1.0), 1.3);
  }

  Phase phase(double elapsed, double fly) {
    Phase ph;
    ph.fly = fly > 0.0 ? std::clamp(elapsed / fly, 0.0, 1.0) : 1.0;
    const double after = std::max(0.0, elapsed - fly);
    ph.land = std::min(1.0, after / POP_MS);
    ph.ripple = std::min(1.0, after / RIPPLE_MS);
    ph.span = std::min(1.0, elapsed / (fly + RIPPLE_MS));
    ph.done = elapsed >= fly + RIPPLE_MS;
    return ph;
  }

  double vertexScale(const Phase& ph) {
    return ph.fly < 1.0 ? flyRadius(ph.fly) : popScale(ph.land);
  }

  // An INSERTED vertex comes from its own foot on the line it split, clamped to the segment, so the
  // bend is pulled out of the stroke instead of appearing beside it (browser motion.js strokeFoot).
  QPointF foot(const core::Point& a, const core::Point& b, double x, double y) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lenSq = dx * dx + dy * dy;
    if (lenSq == 0.0) return QPointF(a.x, a.y);
    const double t = std::clamp(((x - a.x) * dx + (y - a.y) * dy) / lenSq, 0.0, 1.0);
    return QPointF(a.x + t * dx, a.y + t * dy);
  }

  // Send the point at `ptIdx` on its way. `from` defaults to the neighbour it hangs off
  // (the one before, else the one after); a line's first point has none and only pops.
  void Fx::flyIn(int lineIdx, const core::Line& line, int ptIdx, double now, const QPointF* from) {
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

  // Several points at once (a rect's corners): each leaves the one before it and waits for it to
  // land. A range starting at the head has nothing before it, so that first vertex only pops.
  void Fx::flyInRange(int lineIdx, const core::Line& line, int startIdx, int count, double now) {
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
  void Fx::rekey(int fromIdx, int toIdx) {
    for (Flight& f : flights_)
      if (f.lineIdx == fromIdx) f.lineIdx = toIdx;
  }

  // Drop everything that has landed; true while anything is still in the air.
  bool Fx::step(double now) {
    flights_.erase(std::remove_if(flights_.begin(), flights_.end(),
                                  [&](const Flight& f) { return phase(now - f.start, f.fly).done; }),
                   flights_.end());
    return active();
  }

  // The flight a point is on, if any — matched on its resting coordinates.
  const Flight* Fx::at(int lineIdx, const core::Point& p) const {
    for (const Flight& f : flights_) {
      if (f.lineIdx == lineIdx && f.to.x() == p.x && f.to.y() == p.y) return &f;
    }
    return nullptr;
  }

  bool Fx::touches(int lineIdx) const {
    for (const Flight& f : flights_)
      if (f.lineIdx == lineIdx) return true;
    return false;
  }

  void Fx::drop(int lineIdx, const QPointF& to) {
    flights_.erase(std::remove_if(flights_.begin(), flights_.end(),
                                  [&](const Flight& f) {
                                    return f.lineIdx == lineIdx && f.to == to;
                                  }),
                   flights_.end());
  }

}  // namespace stencil::gui::stroke

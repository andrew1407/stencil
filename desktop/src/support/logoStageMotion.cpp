#include "logoStageMotion.hpp"

#include "dustKit.hpp"          // EaseLut
#include "logoStageRules.hpp"   // the tuning

#include <cmath>

namespace stencil::support {

  namespace {
    const EaseLut& easeOut() {
      static const EaseLut lut(0.22, 0.61, 0.36, 1);
      return lut;
    }
    double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }
    double sign(double v) { return v < 0 ? -1.0 : 1.0; }
  }  // namespace

  StagePose revealTween(const StagePose& from, const StagePose& to, double p) {
    const double e = easeOut().at(clamp01(p));
    return StagePose{from.x + (to.x - from.x) * e, from.y + (to.y - from.y) * e,
                     from.size + (to.size - from.size) * e};
  }

  QPointF clampCentre(double x, double y, double size, int w, int h) {
    return QPointF(std::max(size / 2, std::min(w - size / 2, x)),
                   std::max(size / 2, std::min(h - size / 2, y)));
  }

  BounceState bounceState(double rest, double impulse) {
    BounceState st;
    st.size = st.rest = st.from = st.to = rest;
    st.impulse = impulse;
    return st;
  }

  void bounceResize(BounceState& st, double rest, double impulse) {
    const double k = st.rest > 0 ? rest / st.rest : 1.0;
    st.size *= k;
    st.from *= k;
    st.to *= k;
    st.rest = rest;
    st.impulse = impulse;
  }

  // One click carries it a STEP of the way, not the whole way: the mark eases home faster than the
  // steps land, so holding it out at either end takes real clicking.
  void bounceImpulse(BounceState& st, double now) {
    const LogoStageConfig& cfg = logoStageConfig();
    const double lo = std::min(st.rest, st.impulse), hi = std::max(st.rest, st.impulse);
    st.phase = BounceState::Phase::SNAP;
    st.from = st.size;
    st.to = std::clamp(st.size + (st.impulse - st.rest) * cfg.bounceStepShare, lo, hi);
    st.t0 = now;
  }

  double bounceStep(BounceState& st, double now) {
    if (st.phase == BounceState::Phase::REST) return st.size;
    const LogoStageConfig& cfg = logoStageConfig();
    // recoverMs is the time to cross the WHOLE range, so the way home runs at one rate however far
    // the clicks carried it — a short step unwinds in a short time.
    const double range = std::max(1.0, std::abs(st.impulse - st.rest));
    const double span = st.phase == BounceState::Phase::SNAP
        ? cfg.snapMs
        : cfg.recoverMs * (std::abs(st.to - st.from) / range);
    const double p = span > 0 ? clamp01((now - st.t0) / span) : 1.0;
    st.size = st.from + (st.to - st.from) * easeOut().at(p);
    if (p >= 1.0) {
      if (st.phase == BounceState::Phase::SNAP) {
        st.phase = BounceState::Phase::RECOVER;
        st.from = st.size;
        st.to = st.rest;
        st.t0 = now;
      } else {
        st.phase = BounceState::Phase::REST;
      }
    }
    return st.size;
  }

  ChaseState chaseState(double x, double y) {
    ChaseState st;
    st.x = x;
    st.y = y;
    return st;
  }

  void chaseStep(ChaseState& st, const QPointF& cursor, double dt, double size, int w, int h,
                 bool flee) {
    const LogoStageConfig& cfg = logoStageConfig();
    const double stiffness = flee ? cfg.escapeStiffness : cfg.followStiffness;
    const double drag = flee ? cfg.escapeDragPerS : cfg.followDragPerS;
    const double k = dt / 1000.0;
    const double dx = cursor.x() - st.x, dy = cursor.y() - st.y;
    const double dist = std::max(1e-6, std::hypot(dx, dy));
    // Fleeing only answers what is near: past the radius it coasts to a stop on drag alone.
    const double pull = flee ? -stiffness * std::max(0.0, 1 - dist / cfg.escapeRadiusPx) : stiffness;
    st.vx += (dx / dist) * pull * dist * k - st.vx * drag * k;
    st.vy += (dy / dist) * pull * dist * k - st.vy * drag * k;
    st.x += st.vx * k;
    st.y += st.vy * k;
    const double half = size / 2;
    if (st.x < half) { st.x = half; st.vx = std::abs(st.vx); }
    else if (st.x > w - half) { st.x = w - half; st.vx = -std::abs(st.vx); }
    if (st.y < half) { st.y = half; st.vy = std::abs(st.vy); }
    else if (st.y > h - half) { st.y = h - half; st.vy = -std::abs(st.vy); }
  }

  // The way the mark travels, its LENGTH carrying how strongly the tail forms: a mark creeping the
  // last few pixels onto the cursor has to lay no tail, or its whole cloud sits a gap off-centre
  // while it looks still. Zero below the floor, full length at tailFullSpeedPx.
  QPointF headingOfState(double vx, double vy) {
    const LogoStageConfig& cfg = logoStageConfig();
    const double mag = std::hypot(vx, vy);
    if (!(mag > cfg.cloudTailMinSpeedPx)) return QPointF(0, 0);
    const double span = std::max(1.0, cfg.cloudTailFullSpeedPx - cfg.cloudTailMinSpeedPx);
    const double pull = std::min(1.0, (mag - cfg.cloudTailMinSpeedPx) / span);
    return QPointF((vx / mag) * pull, (vy / mag) * pull);
  }

  FlyState flyState(double x, double y, double angle) {
    const double v = logoStageConfig().flySpeedPx;
    return FlyState{x, y, std::cos(angle) * v, std::sin(angle) * v};
  }

  void flyPunch(FlyState& st, double angle) {
    const LogoStageConfig& cfg = logoStageConfig();
    const double v = cfg.flySpeedPx + cfg.flyPunchPx;
    st.vx = std::cos(angle) * v;
    st.vy = std::sin(angle) * v;
  }

  void flyStep(FlyState& st, double dt, double size, int w, int h) {
    const LogoStageConfig& cfg = logoStageConfig();
    const double k = dt / 1000.0, half = size / 2;
    st.x += st.vx * k;
    st.y += st.vy * k;
    if (st.x < half) { st.x = half + (half - st.x); st.vx = std::abs(st.vx); }
    else if (st.x > w - half) { st.x = (w - half) - (st.x - (w - half)); st.vx = -std::abs(st.vx); }
    if (st.y < half) { st.y = half + (half - st.y); st.vy = std::abs(st.vy); }
    else if (st.y > h - half) { st.y = (h - half) - (st.y - (h - half)); st.vy = -std::abs(st.vy); }
    const QPointF c = clampCentre(st.x, st.y, size, w, h);
    st.x = c.x();
    st.y = c.y();
    const double mag = std::hypot(st.vx, st.vy);
    if (mag > cfg.flySpeedPx && mag > 0) {
      const double next = cfg.flySpeedPx + (mag - cfg.flySpeedPx) * std::exp(-cfg.flyDampPerS * k);
      st.vx *= next / mag;
      st.vy *= next / mag;
    }
  }

}  // namespace stencil::support

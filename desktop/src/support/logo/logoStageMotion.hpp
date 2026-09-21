#pragma once
// The logo stage's kinematics, pure and clocked by the caller. Twin of
// browser/js/ui/logo/logoStageMotion.js, value for value.
#include <QPointF>

namespace stencil::support {

  struct StagePose {
    double x = 0, y = 0, size = 0;
  };
  StagePose revealTween(const StagePose& from, const StagePose& to, double p);
  QPointF clampCentre(double x, double y, double size, int w, int h);

  // A click snaps the size to `impulse`, then it recovers to `rest`.
  struct BounceState {
    double size = 0, rest = 0, impulse = 0, from = 0, to = 0, t0 = 0;
    enum class Phase { REST, SNAP, RECOVER } phase = Phase::REST;
  };
  BounceState bounceState(double rest, double impulse);
  // A resize re-measures the mark: every size the bounce carries scales with it, so a snap in
  // flight keeps its shape instead of jumping to the new rest.
  void bounceResize(BounceState& st, double rest, double impulse);
  void bounceImpulse(BounceState& st, double now);
  double bounceStep(BounceState& st, double now);

  // Thrown toward the cursor, or away from it, and dragging behind — it never simply arrives.
  struct ChaseState {
    double x = 0, y = 0, vx = 0, vy = 0;
  };
  ChaseState chaseState(double x, double y);
  void chaseStep(ChaseState& st, const QPointF& cursor, double dt, double size, int w, int h, bool flee);
  // The way it travels, as a unit vector; (0,0) once the cursor has caught it, so a mark that
  // has stopped wears a ring of grain instead of a tail.
  QPointF headingOfState(double vx, double vy);

  struct FlyState {
    double x = 0, y = 0, vx = 0, vy = 0;
  };
  FlyState flyState(double x, double y, double angle);
  void flyPunch(FlyState& st, double angle);
  void flyStep(FlyState& st, double dt, double size, int w, int h);

}  // namespace stencil::support

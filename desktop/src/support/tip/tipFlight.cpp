#include "tipFlight.hpp"
#include "../menu/menuReveal.hpp"
#include "../menu/popupSlide.hpp"
#include "../motionPrefs.hpp"

#include <QLabel>
#include <QPointer>
#include <QWidget>

namespace stencil::support {

  namespace {
    constexpr const char* RUN_PROPERTY = "stencilTipGrowRun";   // the grow in the air; a later show or hide retires it

    int nextRun(QWidget& tip) {
      const int run = tip.property(RUN_PROPERTY).toInt() + 1;
      tip.setProperty(RUN_PROPERTY, run);
      return run;
    }

    // The control's centre: the tip grows out of it and back.
    QPoint originOf(QWidget* owner) { return owner->mapToGlobal(owner->rect().center()); }

    bool growIn(QWidget& tip, const QPoint& origin) {
      dropPopupGhosts(tip);
      const QRect at = tip.geometry();
      QLabel* ghost = popupPictureGhost(tip, at);
      if (!ghost) return false;
      tip.setWindowOpacity(0.0);
      const int run = nextRun(tip);
      QPointer<QWidget> shown(&tip);
      QEasingCurve curve(QEasingCurve::BezierSpline);   // tipGrowIn's cubic-bezier(0.22, 0.61, 0.36, 1)
      curve.addCubicBezierSegment(QPointF(0.22, 0.61), QPointF(0.36, 1.0), QPointF(1.0, 1.0));
      playPopupGhost(ghost, slideStartRect(at, origin, TIP_SCALE, TIP_LIFT), at, motionMs(tipGrowMs()),
                     curve, {{0.0, 0.0}, {TIP_FADE_AT, 1.0}, {1.0, 1.0}}, [shown, run] {
                       if (shown && shown->property(RUN_PROPERTY).toInt() == run && shown->isVisible())
                         shown->setWindowOpacity(1.0);
                     });
      return true;
    }

    bool shrinkOut(QWidget& tip, const QPoint& origin) {
      dropPopupGhosts(tip);   // a grow still in the air goes with it
      nextRun(tip);
      const QRect at = tip.geometry();
      QLabel* ghost = popupPictureGhost(tip, at);
      if (!ghost) return false;
      playPopupGhost(ghost, at, slideStartRect(at, origin, TIP_SCALE, TIP_LIFT), motionMs(tipShrinkMs()),
                     QEasingCurve::InOutSine, {{0.0, 1.0}, {0.6, 0.85}, {1.0, 0.0}});
      return true;
    }
  }  // namespace

  bool growTipAt(QWidget& tip, const QPoint& originGlobal) { return isTransitionMotionOk() && growIn(tip, originGlobal); }
  bool shrinkTipAt(QWidget& tip, const QPoint& originGlobal) { return isTransitionMotionOk() && shrinkOut(tip, originGlobal); }

  bool revealTip(QWidget& tip, QWidget* owner) {
    if (!owner || !owner->isVisible()) return false;
    if (isTransitionMotionOk()) return growIn(tip, originOf(owner));
    // Sliding pours it on the tip's own clock, never a list's 420ms (browser tips.js tipFallbackIn).
    if (isSlideMotionOk()) { slidePopupIn(tip, originOf(owner), tipSlideInMs()); return true; }
    return revealPopup(tip, owner);
  }

  bool dismissTip(QWidget& tip, QWidget* owner) {
    if (!owner || !owner->isVisible()) return false;
    if (isTransitionMotionOk()) return shrinkOut(tip, originOf(owner));
    if (isSlideMotionOk()) return slidePopupOut(tip, owner, originOf(owner), tipSlideOutMs());
    return dismissPopup(tip, owner);
  }

}  // namespace stencil::support

#pragma once
// Transition's tooltip: its picture (menu/popupSlide.hpp popupPictureGhost) grows out of nothing at the control
// it describes to its full size, the tip itself shown as it lands, and shrinks back into nothing — never the
// tip window re-laid at every size of a grow. Browser animations/overlay/overlays.css tipGrowIn / tipShrinkOut.
#include "../tune/motionTable.hpp"

class QPoint;
class QWidget;

namespace stencil::support {

  // The tip's clocks (ms, common/config/motion.json TIP_*): the grow and shrink, and Sliding's pour, half again as
  // long as the grow (user decision).
  inline int tipGrowMs() { return motionTableMs("TIP_GROW_MS", 220); }
  inline int tipShrinkMs() { return motionTableMs("TIP_SHRINK_MS", 180); }
  inline int tipSlideInMs() { return motionTableMs("TIP_SLIDE_IN_MS", 330); }
  inline int tipSlideOutMs() { return motionTableMs("TIP_SLIDE_OUT_MS", 270); }
  inline constexpr double TIP_SCALE = 0.05;    // a speck at the control: it grows all the way (user report)
  inline constexpr double TIP_FADE_AT = 0.2;   // share of the grow by which it is opaque: seen growing
  inline constexpr int TIP_LIFT = 4;           // px the speck sits above the tip's near edge

  // `tip`'s entrance and exit beside `owner` where no particles fly: Transition grows / shrinks it here, any
  // other mode is menuReveal's revealPopup / dismissPopup. False when nothing plays (the caller fades).
  bool revealTip(QWidget& tip, QWidget* owner);
  bool dismissTip(QWidget& tip, QWidget* owner);
  // The same Transition grow and shrink out of a point (GLOBAL) instead of a control, the canvas readout's
  // pointer; false outside Transition or with nothing to photograph.
  bool growTipAt(QWidget& tip, const QPoint& originGlobal);
  bool shrinkTipAt(QWidget& tip, const QPoint& originGlobal);

}  // namespace stencil::support

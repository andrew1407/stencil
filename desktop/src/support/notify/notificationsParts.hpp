#pragma once
// Toast timings, margins and dust flights, private to the Notifications*.cpp TUs.
#include "DisintegrateOverlay.hpp"
#include "modalReveal.hpp"

#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPropertyAnimation>
#include <QRect>
#include <QString>
#include <QWidget>

#include <algorithm>

namespace stencil::gui {

  // QGraphicsOpacityEffect on the label — no compositor dependency.
  inline constexpr int FADE_IN_MS = 270;
  inline constexpr int FADE_OUT_MS = 160;
  // Browser notifyLeave's 14px — the plain fade+rise fallback only.
  inline constexpr int SLIDE_PX = 14;
  // Tighter than the browser's 18px: the window frame already reads as an edge.
  inline constexpr int LEFT_MARGIN = 6;
  inline constexpr int STACK_GAP_PX = 6;
  // Set the moment a toast starts leaving, so the cap ignores it and the exit is never staged twice.
  inline constexpr const char* LEAVING_PROPERTY = "stencilToastLeaving";
  inline constexpr const char* TEXT_PROPERTY = "stencilToastText";
  // The Level the toast was raised at, so a caller's severity is assertable.
  inline constexpr const char* LEVEL_PROPERTY = "stencilToastLevel";

  // Toast dust (browser surface/motion.js surfaceIn/surfaceOut). The entrance runs LONGER than the
  // browser's 680: at the desktop's size the arrival read as a flash rather than a landing.
  inline constexpr int TOAST_IN_MS = 840;
  // Shorter than the entrance. Browser twin: js/ui/notifications.js LEAVE_DUST_MS.
  inline constexpr int TOAST_OUT_MS = 420;


  // Only 0.15 toast-widths past `freeLeft`: farther had every grain off screen within
  // 150ms and the exit read as a cut (browser notifications.js twin).
  inline constexpr double TOAST_REACH = 0.15;
  inline QPoint toastDustPoint(const QRect& r, int freeLeft) {
    return QPoint(freeLeft - qRound(r.width() * TOAST_REACH), r.center().y());
  }

  inline constexpr int DOCK_GAP_PX = 8;

  // `freeLeft` = a left-docked chat's width, 0 without one (browser clipDustToFree twin).
  inline void clipToFree(stencil::gui::DisintegrateOverlay* overlay, QWidget* host, int freeLeft) {
    if (overlay && freeLeft > 0)
      overlay->setPaintClip(QRect(freeLeft, 0, host->width() - freeLeft, host->height()));
  }

  // Returns the flying cloud for reflow()'s retarget, or null when declined.
  inline stencil::gui::DisintegrateOverlay* dustToastIn(QLabel* toast, QGraphicsOpacityEffect* fx,
                                                 QWidget* host, const QRect& rest, int freeLeft) {
    if (!stencil::support::isDustMotionOk()) return nullptr;
    const QPixmap shot = toast->grab();
    if (shot.isNull()) return nullptr;
    auto* overlay = stencil::gui::DisintegrateOverlay::overSurface(
        shot, rest, host, toastDustPoint(rest, freeLeft), /*gather=*/true, TOAST_IN_MS,
        toast->palette().color(QPalette::WindowText));
    if (!overlay) return nullptr;
    clipToFree(overlay, host, freeLeft);
    fx->setOpacity(0.0);
    auto* fade = new QPropertyAnimation(fx, "opacity", toast);
    stencil::gui::holdFadeKeys(fade, TOAST_IN_MS);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
    return overlay;
  }

  // A snapshot with a life of its own, so it can run before the label's own fade/deletion.
  inline bool dustToastOut(QLabel* toast, QWidget* host, int freeLeft) {
    if (!stencil::support::isDustMotionOk()) return false;
    // grab() renders THROUGH the graphics effect, whose cached source can be blank right
    // now — off for the photograph, back on before the fade animates it.
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(toast->graphicsEffect());
    if (fx) fx->setEnabled(false);
    const QPixmap shot = toast->grab();
    if (fx) fx->setEnabled(true);
    if (shot.isNull()) return false;
    auto* overlay = stencil::gui::DisintegrateOverlay::overSurface(
        shot, toast->geometry(), host, toastDustPoint(toast->geometry(), freeLeft),
        /*gather=*/false, TOAST_OUT_MS, toast->palette().color(QPalette::WindowText));
    if (!overlay) return false;
    // No easing override: one curve drives travel AND alpha, and Linear/InCubic both grew a tail.
    clipToFree(overlay, host, freeLeft);
    return true;
  }

}  // namespace stencil::gui

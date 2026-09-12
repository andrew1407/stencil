#pragma once
// The toast stack's timings, margins and dust flights, private to the
// notifications*.cpp TUs.
#include "disintegrateOverlay.hpp"
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

  // Fade durations for the toast lifecycle. Opacity is animated via
  // QGraphicsOpacityEffect on the label — purely Qt-core, so it renders
  // identically on macOS/Windows/Linux with no compositor dependency.
  inline constexpr int kFadeInMs = 180;
  inline constexpr int kFadeOutMs = 160;
  // How far a toast rises in from / drops away to (browser: notifyLeave's 14px) — the
  // plain fade+rise fallback only.
  inline constexpr int kSlidePx = 14;
  // Distance from the host's left edge. Tighter than the browser's 18px: the stack hangs off
  // the WINDOW here, whose frame already reads as an edge, so 18 left it floating mid-canvas.
  inline constexpr int kLeftMargin = 6;
  // Vertical gap between stacked toasts — tight, so a burst reads as one column.
  inline constexpr int kStackGapPx = 6;
  // Set on a toast the moment it starts leaving, so the cap in show() ignores it and its
  // own timer can't stage the exit twice.
  inline constexpr const char* kLeavingProperty = "stencilToastLeaving";
  // The plain message, kept beside the rich text() that carries the glyph.
  inline constexpr const char* kTextProperty = "stencilToastText";

  // Toast dust (browser motion.js surfaceIn/surfaceOut; desktop DisintegrateOverlay).
  // 2x the shared menu clock — a passing notice can afford to drift rather than snap.
  // Leaving is slower still than arriving (browser ENTER_DUST_MS / LEAVE_DUST_MS).
  inline constexpr int kToastInMs = 560;   // browser ENTER_DUST_MS 840 / 1.5
  // Shorter than the entrance, not longer: an arrival can afford to drift, a departure has
  // nothing left to look at. Browser twin: js/ui/notifications.js LEAVE_DUST_MS.
  inline constexpr int kToastOutMs = 420;


  // Past the FREE area's left edge (`freeLeft`: the window's, or a left-docked chat's
  // inner edge), at the toast's own height. Only 0.15 toast-widths past it, not a docked
  // panel's 1.2: the stack already sits at that edge, so a farther point had every grain
  // off screen within 150ms and the exit read as a cut (browser notifications.js twin).
  inline constexpr double kToastReach = 0.15;
  inline QPoint toastDustPoint(const QRect& r, int freeLeft) {
    return QPoint(freeLeft - qRound(r.width() * kToastReach), r.center().y());
  }

  // Room between a left-docked chat and the stack beside it.
  inline constexpr int kDockGapPx = 8;

  // Keep a toast's cloud on the free side of a left-docked chat (`freeLeft` = the
  // panel's width, 0 without one): the point it flies to/from is behind the panel, and
  // unclipped the motes streamed across the composer (browser clipDustToFree twin).
  inline void clipToFree(stencil::gui::DisintegrateOverlay* overlay, QWidget* host, int freeLeft) {
    if (overlay && freeLeft > 0)
      overlay->setPaintClip(QRect(freeLeft, 0, host->width() - freeLeft, host->height()));
  }

  // Returns the flying cloud (so the caller can track it for reflow()'s retarget), or
  // null — declined under STENCIL_NO_ANIM / offscreen.
  inline stencil::gui::DisintegrateOverlay* dustToastIn(QLabel* toast, QGraphicsOpacityEffect* fx,
                                                 QWidget* host, const QRect& rest, int freeLeft) {
    if (!stencil::support::dustMotionOk()) return nullptr;
    const QPixmap shot = toast->grab();
    if (shot.isNull()) return nullptr;
    auto* overlay = stencil::gui::DisintegrateOverlay::overSurface(
        shot, rest, host, toastDustPoint(rest, freeLeft), /*gather=*/true, kToastInMs,
        toast->palette().color(QPalette::WindowText));
    if (!overlay) return nullptr;
    clipToFree(overlay, host, freeLeft);
    fx->setOpacity(0.0);
    auto* fade = new QPropertyAnimation(fx, "opacity", toast);
    stencil::gui::holdFadeKeys(fade, kToastInMs);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
    return overlay;
  }

  // …and the way out — a snapshot with a life of its own, so it can run before the
  // real label's own fade/deletion.
  inline bool dustToastOut(QLabel* toast, QWidget* host, int freeLeft) {
    if (!stencil::support::dustMotionOk()) return false;
    // grab() renders THROUGH the graphics effect, and the effect's cached source can be
    // stale or blank at this moment — the leave then flew a cloud of nothing. Off for the
    // photograph, back on before the fade below animates it (dustToastIn's own rule).
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(toast->graphicsEffect());
    if (fx) fx->setEnabled(false);
    const QPixmap shot = toast->grab();
    if (fx) fx->setEnabled(true);
    if (shot.isNull()) return false;
    auto* overlay = stencil::gui::DisintegrateOverlay::overSurface(
        shot, toast->geometry(), host, toastDustPoint(toast->geometry(), freeLeft),
        /*gather=*/false, kToastOutMs, toast->palette().color(QPalette::WindowText));
    if (!overlay) return false;
    // No easing override — the overlay's default (OutQuint) is what it leaves on. One
    // curve drives the grain's travel AND its alpha, so Linear and InCubic both grew a
    // tail; an ease-out covers the distance early and starts the fade with it.
    clipToFree(overlay, host, freeLeft);
    return true;
  }

}  // namespace stencil::gui

#pragma once
// A GROUP of controls shown/hidden as sand — port of browser js/ui/motion.js
// revealControls / markIn / markOut. Visibility is written SYNCHRONOUSLY both ways and
// the flight is a snapshot, so an interrupted flight leaves nothing half-shown.
#include "DisintegrateOverlay.hpp"
#include "filterFade.hpp"
#include "modalReveal.hpp"

#include <QAbstractAnimation>
#include <QColor>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QLayout>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPropertyAnimation>
#include <QRect>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <utility>

namespace stencil::gui {

  inline constexpr int CONTROL_REVEAL_IN_MS = 347;   // browser REVEAL_GROUP_IN_MS 520 / 1.5
  inline constexpr int CONTROL_REVEAL_OUT_MS = 267;  // browser REVEAL_GROUP_OUT_MS 400 / 1.5
  inline constexpr int CONTROL_REVEAL_CELL_PX = 4;   // browser MARK_COLS x MARK_ROWS
  inline constexpr int CONTROL_REVEAL_MAX_CELLS = 600;
  inline constexpr double CONTROL_REVEAL_SPREAD = 0.32;   // share of a list row's throw
  inline constexpr int CONTROL_REVEAL_PAD_PX = 18;
  inline constexpr double CONTROL_REVEAL_VEIL_STOP = 0.62;   // browser `@keyframes markForm`
  inline constexpr const char* CONTROL_REVEAL_OBJECT_NAME = "stencilControlReveal";
  inline constexpr const char* NO_CONTROL_REVEAL_PROPERTY = "stencilNoControlReveal";
  // The owner's handle on its in-flight cloud: settleReveal runs several times per canvas edit.
  inline constexpr const char* REVEAL_FX_PROPERTY = "stencilRevealFx";
  inline constexpr const char* REVEAL_MAX_WIDTH_PROPERTY = "stencilRevealSavedMax";
  // A slide going the wrong way must be cancelled first, or its finished handler undoes the call.
  inline constexpr const char* REVEAL_SLIDE_PROPERTY = "stencilRevealSlide";
  inline constexpr const char* REVEAL_OPENING_PROPERTY = "stencilRevealOpening";

  namespace ctl {

    void revealGrid(const QSize& size, int* cols, int* rows);

    void trackRevealFx(QWidget* w, DisintegrateOverlay* fx);

    int parkMaxWidth(QWidget* w);
    void handBackMaxWidth(QWidget* w, int natural);

    void trackSlide(QWidget* w, QObject* anim, bool opening);

    void settleReveal(QWidget* w);

    QPixmap groupShot(QWidget* w);

    DisintegrateOverlay* flyReveal(QWidget* w, const QPixmap& pm, const QRect& at,
                                   bool gather, int ms);

    QPixmap markSpecks(const QSize& size, int cols, int rows, qreal dpr,
                       const QColor& bg, const QColor& ink);

  }  // namespace ctl

  inline constexpr const char* MAX_WIDTH_PROPERTY = "maximumWidth";

  void revealControls(QWidget* w, bool show, bool dust = true);

  void paintRevealInPlace(QWidget* w, QWidget* host, bool out,
                          int outMs = 200, int inMs = 260);

  inline constexpr const char* BAR_SLOT_ANIM_NAME = "stencilBarSlot";

  void releaseBarSlot(QWidget* bar);

  void holdBarSlot(QWidget* bar);

  void closeBarSlot(QWidget* bar, int ms);

  // A bar never flies, only its controls: IN is a plain show, OUT waits for their
  // flight then closes the slot. `want()` is asked again on arrival. Browser: motion.js revealBar.
  template <typename Want>
  inline void revealBar(QWidget* bar, Want want) {
    if (!bar) return;
    const bool wanted = want();
    if (wanted || support::motionReduced() || !bar->isVisible()) {
      // A bar asked back mid-close is still frozen where the slide left it.
      releaseBarSlot(bar);
      bar->setVisible(wanted);
      return;
    }
    holdBarSlot(bar);
    // `bar` is the context object: the job dies with its dialog.
    QTimer::singleShot(CONTROL_REVEAL_OUT_MS, bar, [bar, want = std::move(want)] {
      if (!want()) closeBarSlot(bar, CONTROL_REVEAL_OUT_MS);
      else releaseBarSlot(bar);
    });
  }

  void stopDustClouds(QWidget* host);

}  // namespace stencil::gui

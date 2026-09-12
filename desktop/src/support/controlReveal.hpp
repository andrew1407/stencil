#pragma once
// Showing and hiding a GROUP of controls as sand — the f(x,y) transform fields and the
// custom page's W/H boxes, each governed by another control that stays exactly where it
// is. Desktop port of browser js/ui/motion.js revealControls / markIn / markOut.
//
// The same engine as a removed row's scatter (disintegrateOverlay.hpp) and the
// checkbox's indicator (controlSwap.hpp), on a mark's clock and a mark's throw: a group
// of fields is not a window, so its motes stay near the box they came from. The group's
// own visibility is written SYNCHRONOUSLY in both directions — the layout never waits
// for a decoration — and the picture that flies is a snapshot, so nothing can be left
// half-shown if the flight is interrupted.
//
// Header-only and Q_OBJECT-free (no signals/slots of its own), so it needs no MOC.
#include "disintegrateOverlay.hpp"
#include "filterFade.hpp"    // kFilterDustObjectName — an arrival's cloud counts too
#include "modalReveal.hpp"   // support::motionReduced()

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

  // A mark's clock: the arrival is the half you watch, the departure is brisk (browser
  // MARK_IN_MS / MARK_OUT_MS).
  // A group's slot is a wider move than a single mark and reads as a snap at the mark's
  // clock, so it gets its own. Browser twin: REVEAL_GROUP_IN/OUT_MS.
  inline constexpr int kControlRevealInMs = 347;   // browser REVEAL_GROUP_IN_MS 520 / 1.5
  inline constexpr int kControlRevealOutMs = 267;  // browser REVEAL_GROUP_OUT_MS 400 / 1.5
  // Motes about this big on screen, under a ceiling of their own — well below a window's:
  // the f(x,y) row is a few hundred pixels wide and a window's grain over it would build
  // thousands of cells for a third of a second (browser MARK_COLS x MARK_ROWS).
  inline constexpr int kControlRevealCellPx = 4;
  inline constexpr int kControlRevealMaxCells = 600;
  // The throw, as a share of a list row's — the same reduction the checkbox indicator
  // takes (kCheckSwapSpread): a toolbar group flinging motes 80px would read as the
  // window coming apart.
  inline constexpr double kControlRevealSpread = 0.32;
  // Room around the group for the motes to fly into. The far tail is transparent well
  // before it reaches this, so nothing visible is ever cut off.
  inline constexpr int kControlRevealPadPx = 18;
  // Where the real group waits while its motes gather — the browser's `@keyframes
  // markForm`, which holds it back until they have very nearly landed.
  inline constexpr double kControlRevealVeilStop = 0.62;
  inline constexpr const char* kControlRevealObjectName = "stencilControlReveal";
  // Set on a group that must not play this (nothing does yet; the escape hatch matches
  // controlSwap's kNoControlSwapProperty).
  inline constexpr const char* kNoControlRevealProperty = "stencilNoControlReveal";
  // The owner's handle on its one in-flight cloud, so settleReveal is O(1) instead of
  // a findChildren scan of the whole window (it runs several times per canvas edit).
  inline constexpr const char* kRevealFxProperty = "stencilRevealFx";
  // …and the layout's own maximumWidth, parked while a slide holds it at 0 (see parkMaxWidth).
  inline constexpr const char* kRevealMaxWidthProperty = "stencilRevealSavedMax";
  // The width slide and its direction: one going the wrong way must be cancelled before the
  // next reveal, or its finished handler undoes that call.
  inline constexpr const char* kRevealSlideProperty = "stencilRevealSlide";
  inline constexpr const char* kRevealOpeningProperty = "stencilRevealOpening";

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

  // Property name shared by both flights' width animation.
  inline constexpr const char* kMaxWidthProperty = "maximumWidth";

  void revealControls(QWidget* w, bool show, bool dust = true);

  void paintRevealInPlace(QWidget* w, QWidget* host, bool out,
                          int outMs = 200, int inMs = 260);

  // The height slide a leaving bar rides (below), named so a bar coming back can cancel it.
  inline constexpr const char* kBarSlotAnimName = "stencilBarSlot";

  void releaseBarSlot(QWidget* bar);

  void holdBarSlot(QWidget* bar);

  void closeBarSlot(QWidget* bar, int ms);

  // A BAR holding revealed controls (the selection strips): the bar itself never flies —
  // only its controls do — so on the way IN this is a plain show (the slot they fly into),
  // and on the way OUT it is deferred by their flight and then closes its slot. Taking the
  // strip away at once took Select all's own out-flight off the screen before a frame of
  // it showed. `want()` is the single source of whether the bar belongs,
  // asked now and again on arrival, so a selection made mid-flight keeps it.
  // Browser twin: motion.js revealBar.
  template <typename Want>   // a template, so the predicate never lands on the heap
  inline void revealBar(QWidget* bar, Want want) {
    if (!bar) return;
    const bool wanted = want();
    if (wanted || support::motionReduced() || !bar->isVisible()) {
      // A bar asked back mid-close is still frozen (or part-closed) at whatever the
      // slide left it, so give its height back to the layout before showing it again.
      releaseBarSlot(bar);
      bar->setVisible(wanted);
      return;
    }
    holdBarSlot(bar);   // …so its controls' departure cannot collapse it first
    // `bar` is the timer's context object, so the job dies with the dialog that owns it.
    QTimer::singleShot(kControlRevealOutMs, bar, [bar, want = std::move(want)] {
      if (!want()) closeBarSlot(bar, kControlRevealOutMs);
      else releaseBarSlot(bar);   // wanted again mid-wait: unfreeze, it stays
    });
  }

  void stopDustClouds(QWidget* host);

}  // namespace stencil::gui

#include "controlReveal.hpp"

namespace stencil::gui {

  // Give the bar's height back to the layout — after the slide, or when the bar is asked
  // back while one is still running.
  void releaseBarSlot(QWidget* bar) {
    if (!bar) return;
    if (auto* live = bar->findChild<QPropertyAnimation*>(QString::fromLatin1(kBarSlotAnimName))) {
      live->stop();
      delete live;
    }
    bar->setMinimumHeight(0);
    bar->setMaximumHeight(QWIDGETSIZE_MAX);
  }


  // Pin the bar's slot at the height it has RIGHT NOW. Its controls are about to fly out,
  // and the last one to be hidden takes the strip's content height with it — the bar
  // collapsing to nothing in that one frame IS the jump, before any slide of ours could
  // start. Frozen, the strip keeps its shape while they leave.
  void holdBarSlot(QWidget* bar) {
    if (!bar || !bar->isVisible()) return;
    const int h = bar->height();
    if (h <= 0) return;
    bar->setMinimumHeight(h);
    bar->setMaximumHeight(h);
  }


  // …and then the slide: the strip's SLOT closes on its own curve instead of the strip
  // blinking out and dropping everything below it upward in one frame. The bar sits
  // directly above a list, so that drop moved the first row out from under the eye (user
  // report: the pinned "Temporary (unsaved)" row jumped when Select all left). The gap
  // under the bar is the BAR's own bottom margin (projectsDialog gives it one, in a
  // zero-spacing slot with the list), so the whole footprint goes with the height and
  // nothing is left over to fall away at the end. Min AND max ride the value together —
  // a cap alone cannot hold a slot open, and a floor alone cannot close it.
  void closeBarSlot(QWidget* bar, int ms) {
    if (!bar || !bar->isVisible()) return;
    const int h = bar->height();
    if (h <= 0 || support::motionReduced()) {
      releaseBarSlot(bar);
      bar->setVisible(false);
      return;
    }
    auto* shrink = new QPropertyAnimation(bar, "minimumHeight", bar);
    shrink->setObjectName(QString::fromLatin1(kBarSlotAnimName));
    shrink->setDuration(ms);
    shrink->setStartValue(h);
    shrink->setEndValue(0);
    // The same gentle S a control's own slot closes on (revealControls) — a strong
    // ease-in barely moves, then snaps, which is the jump this exists to remove.
    shrink->setEasingCurve(QEasingCurve::InOutCubic);
    QPointer<QWidget> guard(bar);
    QObject::connect(shrink, &QPropertyAnimation::valueChanged, bar,
                     [guard](const QVariant& v) { if (guard) guard->setMaximumHeight(v.toInt()); });
    QObject::connect(shrink, &QPropertyAnimation::finished, bar, [guard] {
      if (!guard) return;
      guard->setVisible(false);
      releaseBarSlot(guard);   // hand sizing back to the layout
    });
    shrink->start(QAbstractAnimation::DeleteWhenStopped);
  }


  // Stop every cloud still in the air over `host` — a row's removal dust, a row ARRIVING
  // out of the filter's sand, a bar's controls coming or going. Called as a dialog closes
  // (the close flight re-photographs it as it hides — modalReveal — and a live cloud would
  // be carried on after the window is gone) and whenever the layout under the clouds is
  // pulled away: entering or leaving fullscreen hides every toolbar, and a cloud started
  // by one of those controls was left flying over the bare canvas.
  void stopDustClouds(QWidget* host) {
    if (!host) return;
    for (const char* name : {DisintegrateOverlay::kObjectName, kControlRevealObjectName,
                             kFilterDustObjectName})
      for (QWidget* fx : host->findChildren<QWidget*>(QString::fromLatin1(name))) {
        fx->hide();   // excluded from the ghost's render immediately; deleted safely after
        fx->deleteLater();
      }
  }
}  // namespace stencil::gui

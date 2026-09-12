#include "controlReveal.hpp"

namespace stencil::gui {

  void releaseBarSlot(QWidget* bar) {
    if (!bar) return;
    if (auto* live = bar->findChild<QPropertyAnimation*>(QString::fromLatin1(BAR_SLOT_ANIM_NAME))) {
      live->stop();
      delete live;
    }
    bar->setMinimumHeight(0);
    bar->setMaximumHeight(QWIDGETSIZE_MAX);
  }


  // Frozen at its current height: the last control hidden takes the strip's content
  // height with it, and that one-frame collapse IS the jump.
  void holdBarSlot(QWidget* bar) {
    if (!bar || !bar->isVisible()) return;
    const int h = bar->height();
    if (h <= 0) return;
    bar->setMinimumHeight(h);
    bar->setMaximumHeight(h);
  }


  // The slot closes on its own curve, or everything below jumps up a frame. Min AND max
  // ride the value together — a cap alone cannot hold a slot open, a floor alone cannot close it.
  void closeBarSlot(QWidget* bar, int ms) {
    if (!bar || !bar->isVisible()) return;
    const int h = bar->height();
    if (h <= 0 || support::motionReduced()) {
      releaseBarSlot(bar);
      bar->setVisible(false);
      return;
    }
    auto* shrink = new QPropertyAnimation(bar, "minimumHeight", bar);
    shrink->setObjectName(QString::fromLatin1(BAR_SLOT_ANIM_NAME));
    shrink->setDuration(ms);
    shrink->setStartValue(h);
    shrink->setEndValue(0);
    // The same S as revealControls: a strong ease-in barely moves, then snaps.
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


  // Called as a dialog closes (modalReveal re-photographs it as it hides) and whenever
  // the layout under the clouds is pulled away (fullscreen hides every toolbar).
  void stopDustClouds(QWidget* host) {
    if (!host) return;
    for (const char* name : {DisintegrateOverlay::OBJECT_NAME, CONTROL_REVEAL_OBJECT_NAME,
                             FILTER_DUST_OBJECT_NAME})
      for (QWidget* fx : host->findChildren<QWidget*>(QString::fromLatin1(name))) {
        fx->hide();   // excluded from the ghost's render immediately; deleted safely after
        fx->deleteLater();
      }
  }
}  // namespace stencil::gui

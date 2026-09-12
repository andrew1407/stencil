#include "notifications.hpp"
#include "notificationsParts.hpp"
#include "disintegrateOverlay.hpp"
#include "iconSet.hpp"
#include "modalReveal.hpp"  // stencil::support::motionReduced()
#include <algorithm>
#include <QBuffer>
#include <QGuiApplication>
#include <QByteArray>
#include <QEasingCurve>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPixmap>
#include <QPointer>
#include <QPropertyAnimation>
#include <QTimer>
#include <QWidget>


namespace stencil::gui {

  Notifications::Notifications(QWidget* host) : QObject(host), host_(host) {
    // Watch the host so toasts recenter when it resizes (see eventFilter).
    if (host_) host_->installEventFilter(this);
  }

  void Notifications::setBottomInset(int px) {
    if (bottomInset_ == px) return;
    bottomInset_ = px;
    reflow();
  }

  void Notifications::setLeftInset(int px) {
    if (leftInset_ == px) return;
    leftInset_ = px;
    reflow();
  }

  void Notifications::setColors(const QColor& normal, const QColor& error) {
    normalBg_ = normal;
    errorBg_ = error;
  }

  // Play `toast` out and delete it. Called both by its own timer and by the cap in
  // show(); the flag makes the second call a no-op rather than a second exit animation
  // stacked on the first.
  void Notifications::dismiss(QLabel* toast) {
    if (!toast || toast->property(kLeavingProperty).toBool()) return;
    toast->setProperty(kLeavingProperty, true);
    entering_.remove(toast);   // no longer entering — reflow() must not chase it anymore
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(toast->graphicsEffect());
    if (!fx) {   // no effect to animate (defensive): drop it straight away
      stack_.removeAll(QPointer<QLabel>(toast));
      toast->deleteLater();
      QTimer::singleShot(0, this, [this] { reflow(); });
      return;
    }
    // Sand first; a decline falls back to the plain drop-away below.
    const bool dusted = dustToastOut(toast, host_, leftInset_);
    auto* fadeOut = new QPropertyAnimation(fx, "opacity", toast);
    fadeOut->setDuration(kFadeOutMs);
    fadeOut->setStartValue(fx->opacity());
    fadeOut->setEndValue(0.0);
    fadeOut->setEasingCurve(QEasingCurve::InCubic);
    if (!dusted) {
      auto* dropOut = new QPropertyAnimation(toast, "geometry", toast);
      dropOut->setDuration(kFadeOutMs);
      dropOut->setStartValue(toast->geometry());
      dropOut->setEndValue(toast->geometry().translated(0, kSlidePx));
      dropOut->setEasingCurve(QEasingCurve::InCubic);
      dropOut->start(QAbstractAnimation::DeleteWhenStopped);
    }
    QObject::connect(fadeOut, &QPropertyAnimation::finished, this,
                     [this, toast] {
                       stack_.removeAll(QPointer<QLabel>(toast));
                       toast->deleteLater();
                       QTimer::singleShot(0, this, [this] { reflow(); });
                     });
    fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
  }

  // Stack every live toast in the BOTTOM-LEFT of the host (mirrors the browser's
  // toast position), newest at the bottom, growing upward. Anchored to the bottom so
  // an eventFilter resize keeps them pinned there.
  void Notifications::reflow() {
    if (!host_) return;
    // Only the standing toasts get a slot. A leaving one is being carried by its own
    // geometry animation — restacking it would fight that animation, and holding its slot
    // open would leave a gap (and a fourth toast's worth of space) while it faded.
    const auto toasts = liveToasts();
    int y = host_->height() - 12 - bottomInset_;   // bottom margin, clear of any status bar
    for (int i = toasts.size() - 1; i >= 0; --i) {
      QLabel* t = toasts[i];
      y -= t->height();
      const QRect rest(kLeftMargin + leftInset_ + (leftInset_ > 0 ? kDockGapPx : 0),
                       std::max(8, y), t->width(), t->height());
      auto* rise = t->findChild<QPropertyAnimation*>("toastRise");
      if (rise && rise->state() == QAbstractAnimation::Running) {
        // Still rising: retarget the flight rather than move()ing underneath it.
        rise->setStartValue(rest.translated(0, kSlidePx));
        rise->setEndValue(rest);
      } else {
        // A still-flying entrance cloud was grabbed at the OLD box; drag it along by the
        // same delta so a burst that bumps this toast to a new slot doesn't strand the
        // motes at a stale position while the (invisible-till-they-land) widget jumps.
        const QPoint delta = rest.topLeft() - t->geometry().topLeft();
        if (QPointer<DisintegrateOverlay> overlay = entering_.value(t)) overlay->retarget(delta);
        t->move(rest.topLeft());
      }
      t->raise();
      y -= kStackGapPx;              // gap between stacked toasts
    }
  }

  bool Notifications::eventFilter(QObject* watched, QEvent* event) {
    if (watched == host_ && event->type() == QEvent::Resize) reflow();
    return QObject::eventFilter(watched, event);
  }
}


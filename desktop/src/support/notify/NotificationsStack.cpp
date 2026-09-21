#include "Notifications.hpp"
#include "notificationsParts.hpp"
#include "DisintegrateOverlay.hpp"
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

  Notifications::Notifications(QWidget* host) : QObject(host), host(host) {
    if (this->host) this->host->installEventFilter(this);
  }

  void Notifications::setBottomInset(int px) {
    if (bottomInset == px) return;
    bottomInset = px;
    reflow();
  }

  void Notifications::setLeftInset(int px) {
    if (leftInset == px) return;
    leftInset = px;
    reflow();
  }

  void Notifications::setColors(const QColor& normal, const QColor& error) {
    normalBg = normal;
    errorBg = error;
  }

  // Called by its own timer AND by the cap in show(); the flag makes the second a no-op.
  void Notifications::dismiss(QLabel* toast) {
    if (!toast || toast->property(LEAVING_PROPERTY).toBool()) return;
    toast->setProperty(LEAVING_PROPERTY, true);
    entering.remove(toast);   // no longer entering — reflow() must not chase it anymore
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(toast->graphicsEffect());
    if (!fx) {   // no effect to animate (defensive): drop it straight away
      stack.removeAll(QPointer<QLabel>(toast));
      toast->deleteLater();
      QTimer::singleShot(0, this, [this] { reflow(); });
      return;
    }
    const bool dusted = dustToastOut(toast, host, leftInset);
    auto* fadeOut = new QPropertyAnimation(fx, "opacity", toast);
    fadeOut->setDuration(FADE_OUT_MS);
    fadeOut->setStartValue(fx->opacity());
    fadeOut->setEndValue(0.0);
    fadeOut->setEasingCurve(QEasingCurve::InCubic);
    if (!dusted) {
      auto* dropOut = new QPropertyAnimation(toast, "geometry", toast);
      dropOut->setDuration(FADE_OUT_MS);
      dropOut->setStartValue(toast->geometry());
      dropOut->setEndValue(toast->geometry().translated(0, SLIDE_PX));
      dropOut->setEasingCurve(QEasingCurve::InCubic);
      dropOut->start(QAbstractAnimation::DeleteWhenStopped);
    }
    QObject::connect(fadeOut, &QPropertyAnimation::finished, this,
                     [this, toast] {
                       stack.removeAll(QPointer<QLabel>(toast));
                       toast->deleteLater();
                       QTimer::singleShot(0, this, [this] { reflow(); });
                     });
    fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
  }

  // Bottom-left (browser parity), newest at the bottom, growing upward.
  void Notifications::reflow() {
    if (!host) return;
    // A leaving toast is carried by its own geometry animation; holding its slot would leave a gap.
    const auto toasts = liveToasts();
    int y = host->height() - 12 - bottomInset;   // bottom margin, clear of any status bar
    for (int i = toasts.size() - 1; i >= 0; --i) {
      QLabel* t = toasts[i];
      y -= t->height();
      const QRect rest(LEFT_MARGIN + leftInset + (leftInset > 0 ? DOCK_GAP_PX : 0),
                       std::max(8, y), t->width(), t->height());
      auto* rise = t->findChild<QPropertyAnimation*>("toastRise");
      if (rise && rise->state() == QAbstractAnimation::Running) {
        rise->setStartValue(rest.translated(0, SLIDE_PX));
        rise->setEndValue(rest);
      } else {
        // An entrance cloud grabbed at the OLD box is dragged along by the same delta.
        const QPoint delta = rest.topLeft() - t->geometry().topLeft();
        if (QPointer<DisintegrateOverlay> overlay = entering.value(t)) overlay->retarget(delta);
        t->move(rest.topLeft());
      }
      t->raise();
      y -= STACK_GAP_PX;              // gap between stacked toasts
    }
  }

  bool Notifications::eventFilter(QObject* watched, QEvent* event) {
    if (watched == host && event->type() == QEvent::Resize) reflow();
    return QObject::eventFilter(watched, event);
  }
}


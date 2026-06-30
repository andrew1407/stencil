#include "notifications.hpp"
#include <algorithm>
#include <QEasingCurve>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPropertyAnimation>
#include <QTimer>
#include <QWidget>

namespace {
  // Fade durations for the toast lifecycle. Short enough to feel instant, long
  // enough to read as motion. Opacity is animated via QGraphicsOpacityEffect on
  // the (child) label — purely Qt-core, so it renders identically on macOS,
  // Windows and Linux with no compositor/window-manager dependency.
  constexpr int kFadeInMs = 180;
  constexpr int kFadeOutMs = 160;
}  // namespace

namespace stencil::gui {

  Notifications::Notifications(QWidget* host) : QObject(host), host_(host) {
    // Watch the host so toasts recenter when it resizes (see eventFilter).
    if (host_) host_->installEventFilter(this);
  }

  void Notifications::show(const QString& text, Level level, int msec) {
    if (!host_) return;

    // Colors mirror the browser toast variants (info/success/error).
    QString bg;
    if (level == Level::Success) {
      bg = "#28a745";
    } else if (level == Level::Error) {
      bg = "#dc3545";
    } else if (level == Level::Info) {
      bg = "#7c3aed";
    }

    auto* toast = new QLabel(text, host_);
    toast->setObjectName("toast");
    toast->setStyleSheet(QString("QLabel#toast { background: %1; color: white; "
                                 "padding: 8px 14px; border-radius: 6px; }")
                             .arg(bg));
    toast->setAttribute(Qt::WA_TransparentForMouseEvents);
    // Resolve the stylesheet (padding/font) before measuring; otherwise
    // adjustSize() sizes against the unstyled label and the text gets clipped.
    toast->ensurePolished();
    toast->adjustSize();

    // Fade the toast in. The effect is owned by the label (setGraphicsEffect
    // takes ownership) and the animation deletes itself when it stops, so this
    // adds no lifetime bookkeeping to the toast's existing delete-on-timeout.
    auto* fx = new QGraphicsOpacityEffect(toast);
    fx->setOpacity(0.0);
    toast->setGraphicsEffect(fx);
    toast->show();
    toast->raise();
    reflow();
    auto* fadeIn = new QPropertyAnimation(fx, "opacity", toast);
    fadeIn->setDuration(kFadeInMs);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setEasingCurve(QEasingCurve::OutCubic);
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);

    QTimer::singleShot(msec, toast, [this, toast, fx] {
      // Fade out, then delete and reflow the survivors.
      auto* fadeOut = new QPropertyAnimation(fx, "opacity", toast);
      fadeOut->setDuration(kFadeOutMs);
      fadeOut->setStartValue(fx->opacity());
      fadeOut->setEndValue(0.0);
      fadeOut->setEasingCurve(QEasingCurve::InCubic);
      QObject::connect(fadeOut, &QPropertyAnimation::finished, this,
                       [this, toast] {
                         toast->deleteLater();
                         QTimer::singleShot(0, this, [this] { reflow(); });
                       });
      fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
    });
  }

  // Stack every live toast in the BOTTOM-LEFT of the host (mirrors the browser's
  // toast position), newest at the bottom, growing upward. Anchored to the bottom so
  // an eventFilter resize keeps them pinned there.
  void Notifications::reflow() {
    if (!host_) return;
    const auto toasts = host_->findChildren<QLabel*>("toast",
                                                     Qt::FindDirectChildrenOnly);
    int y = host_->height() - 12;   // bottom margin
    for (int i = toasts.size() - 1; i >= 0; --i) {
      QLabel* t = toasts[i];
      y -= t->height();
      t->move(12, std::max(8, y));   // left margin
      t->raise();
      y -= 8;                        // gap between stacked toasts
    }
  }

  bool Notifications::eventFilter(QObject* watched, QEvent* event) {
    if (watched == host_ && event->type() == QEvent::Resize) reflow();
    return QObject::eventFilter(watched, event);
  }

}

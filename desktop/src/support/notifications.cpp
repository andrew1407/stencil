#include "notifications.hpp"
#include "iconSet.hpp"
#include <algorithm>
#include <QBuffer>
#include <QGuiApplication>
#include <QByteArray>
#include <QEasingCurve>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPropertyAnimation>
#include <QTimer>
#include <QWidget>

namespace {
  // Fade durations for the toast lifecycle. Opacity is animated via
  // QGraphicsOpacityEffect on the label — purely Qt-core, so it renders
  // identically on macOS/Windows/Linux with no compositor dependency.
  constexpr int kFadeInMs = 180;
  constexpr int kFadeOutMs = 160;
  // How far a toast rises in from / drops away to (browser: notifyLeave's 14px).
  constexpr int kSlidePx = 14;
  // Distance from the host's left edge. Tighter than the browser's 18px: the stack hangs off
  // the WINDOW here, whose frame already reads as an edge, so 18 left it floating mid-canvas.
  constexpr int kLeftMargin = 6;
  // Vertical gap between stacked toasts — tight, so a burst reads as one column.
  constexpr int kStackGapPx = 6;
  // Set on a toast the moment it starts leaving, so the cap in show() ignores it and its
  // own timer can't stage the exit twice.
  constexpr const char* kLeavingProperty = "stencilToastLeaving";
  // The plain message, kept beside the rich text() that carries the glyph.
  constexpr const char* kTextProperty = "stencilToastText";
}  // namespace

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

  // Middle-ellipsize any whitespace-free run (a filename, a URL) longer than 48 chars,
  // so a toast stays a small balloon — full names belong in lists and tooltips, not in
  // transient messages (browser squeezeLongTokens parity).
  static QString squeezeLongTokens(const QString& text) {
    constexpr int kMax = 48;
    const QStringList parts = text.split(QLatin1Char(' '));
    QStringList out;
    out.reserve(parts.size());
    for (const QString& tok : parts) {
      if (tok.size() <= kMax) { out << tok; continue; }
      const int keep = (kMax - 1) / 2;
      out << tok.left(keep) + QChar(0x2026) + tok.right(keep);
    }
    return out.join(QLatin1Char(' '));
  }

  void Notifications::show(const QString& rawText, Level level, int msec) {
    if (!host_) return;
    const QString text = squeezeLongTokens(rawText);

    // Coalesce: an identical message already standing never stacks a copy — the
    // standing toast just lives longer, with no entrance replay (browser parity:
    // one balloon per message).
    for (QLabel* t : liveToasts())
      if (t->property(kTextProperty).toString() == text) {
        if (auto* life = t->findChild<QTimer*>("toastLife")) life->start(msec);
        return;
      }

    // Cap the stack BEFORE adding this one, so a burst never walls off the corner.
    // Oldest go first; only standing toasts count — one already playing its exit
    // shouldn't push a live one off the stack.
    const QList<QLabel*> live = liveToasts();
    for (int i = 0; i < live.size() + 1 - kMaxVisible && i < live.size(); ++i)
      dismiss(live[i]);

    // Colour + glyph mirror the browser toast variants exactly: --danger for a failure,
    // --accent for everything else, with a check / x / info mark telling the kinds apart.
    const QColor bg = level == Level::Error ? errorBg_ : normalBg_;
    const char* glyph = level == Level::Success ? "check" : level == Level::Error ? "x" : "info";

    auto* toast = new QLabel(host_);
    toast->setTextFormat(Qt::RichText);
    // The glyph rides in the label as an inline image, so one widget carries the
    // whole toast. (guiHelpers::inlineIconHtml is the same recipe, but this target
    // links a deliberately minimal source set — see stencil_notifications_headless.)
    QByteArray png;
    {
      QBuffer buf(&png);
      buf.open(QIODevice::WriteOnly);
      // The dpr-AWARE overload — pixmap(16, 16) would scale the Retina raster back
      // down to 16 device pixels and show a soft glyph on a hi-dpi screen.
      const qreal dpr = qApp ? qApp->devicePixelRatio() : qreal(1);
      themedIcon(QString::fromLatin1(glyph), QColor(Qt::white), 16, /*shadow=*/false, dpr)
          .pixmap(QSize(16, 16), dpr)
          .toImage()
          .save(&buf, "PNG");
    }
    toast->setText(QString("<img src=\"data:image/png;base64,%1\" width=\"16\" height=\"16\">"
                           "&nbsp;&nbsp;%2")
                       .arg(QString::fromLatin1(png.toBase64()), text.toHtmlEscaped()));
    // The message on its own — text() is now markup wrapping an inline glyph, so anything
    // reading a toast back (the GUI tests) has a plain string to compare.
    toast->setProperty(kTextProperty, text);
    toast->setObjectName("toast");
    toast->setStyleSheet(QString("QLabel#toast { background: %1; color: white; "
                                 "padding: 8px 14px; border-radius: 6px; }")
                             .arg(bg.name()));
    toast->setAttribute(Qt::WA_TransparentForMouseEvents);
    // Resolve the stylesheet (padding/font) before measuring; otherwise
    // adjustSize() sizes against the unstyled label and the text gets clipped.
    toast->ensurePolished();
    toast->adjustSize();
    // Never wider than the browser toast cap (or the host, if narrower) — a long
    // message wraps inside the balloon instead of running out of bounds.
    const int cap = qMin(380, qMax(120, host_->width() - 36));
    if (toast->width() > cap) {
      toast->setWordWrap(true);
      toast->setFixedWidth(cap);
      toast->adjustSize();
      toast->setFixedHeight(toast->heightForWidth(cap));
    }

    // Fade the toast in. The effect is owned by the label (setGraphicsEffect
    // takes ownership) and the animation deletes itself when it stops, so this
    // adds no lifetime bookkeeping to the toast's existing delete-on-timeout.
    auto* fx = new QGraphicsOpacityEffect(toast);
    fx->setOpacity(0.0);
    toast->setGraphicsEffect(fx);
    toast->show();
    toast->raise();
    stack_.push_back(toast);
    reflow();
    auto* fadeIn = new QPropertyAnimation(fx, "opacity", toast);
    fadeIn->setDuration(kFadeInMs);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setEasingCurve(QEasingCurve::OutCubic);
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
    // …and rises into place while it does (browser parity: the balloon's springy
    // entrance). Geometry, not a transform — a QLabel has no transform to animate.
    const QRect rest = toast->geometry();
    auto* riseIn = new QPropertyAnimation(toast, "geometry", toast);
    // Named so reflow() can find it: a toast that arrives while this one is still rising
    // restacks it, and an entrance animation left pointing at the OLD slot would drag it
    // back down there the moment it finished — a burst ended up piled in one place.
    riseIn->setObjectName("toastRise");
    riseIn->setDuration(kFadeInMs + 80);
    riseIn->setStartValue(rest.translated(0, kSlidePx));
    riseIn->setEndValue(rest);
    riseIn->setEasingCurve(QEasingCurve::OutBack);
    riseIn->start(QAbstractAnimation::DeleteWhenStopped);

    // A named, restartable lifetime timer (not an anonymous singleShot), so the
    // coalescing branch above can extend a standing toast instead of stacking one.
    auto* life = new QTimer(toast);
    life->setObjectName("toastLife");
    life->setSingleShot(true);
    connect(life, &QTimer::timeout, toast, [this, toast] { dismiss(toast); });
    life->start(msec);
  }

  // Every toast still standing, oldest first — insertion order (see stack_), with the
  // ones already playing their exit filtered out by kLeavingProperty.
  QList<QLabel*> Notifications::liveToasts() const {
    QList<QLabel*> live;
    for (const QPointer<QLabel>& t : stack_)
      if (t && !t->property(kLeavingProperty).toBool()) live.push_back(t.data());
    return live;
  }

  // Play `toast` out and delete it. Called both by its own timer and by the cap in
  // show(); the flag makes the second call a no-op rather than a second exit animation
  // stacked on the first.
  void Notifications::dismiss(QLabel* toast) {
    if (!toast || toast->property(kLeavingProperty).toBool()) return;
    toast->setProperty(kLeavingProperty, true);
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(toast->graphicsEffect());
    if (!fx) {   // no effect to animate (defensive): drop it straight away
      stack_.removeAll(QPointer<QLabel>(toast));
      toast->deleteLater();
      QTimer::singleShot(0, this, [this] { reflow(); });
      return;
    }
    auto* fadeOut = new QPropertyAnimation(fx, "opacity", toast);
    fadeOut->setDuration(kFadeOutMs);
    fadeOut->setStartValue(fx->opacity());
    fadeOut->setEndValue(0.0);
    fadeOut->setEasingCurve(QEasingCurve::InCubic);
    // Drop away as it goes, rather than fading in place — the browser's
    // .notify-leaving, which deliberately doesn't replay the entrance backwards.
    auto* dropOut = new QPropertyAnimation(toast, "geometry", toast);
    dropOut->setDuration(kFadeOutMs);
    dropOut->setStartValue(toast->geometry());
    dropOut->setEndValue(toast->geometry().translated(0, kSlidePx));
    dropOut->setEasingCurve(QEasingCurve::InCubic);
    dropOut->start(QAbstractAnimation::DeleteWhenStopped);
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
      const QRect rest(kLeftMargin + leftInset_, std::max(8, y), t->width(), t->height());
      auto* rise = t->findChild<QPropertyAnimation*>("toastRise");
      if (rise && rise->state() == QAbstractAnimation::Running) {
        // Still rising: retarget the flight rather than move()ing underneath it.
        rise->setStartValue(rest.translated(0, kSlidePx));
        rise->setEndValue(rest);
      } else {
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

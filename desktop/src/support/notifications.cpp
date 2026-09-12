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

    // A repeat of the same message can land while the LAST one is already leaving (e.g. a
    // debounced "Saved" firing again mid-exit-flight). liveToasts() above only coalesces into
    // a standing one, so the fresh, fully-opaque label buried the leaving one's still-running
    // dust — reading as the message vanishing with no dust. Finish the old
    // label outright instead; its dust cloud (a separate overlay this doesn't touch) keeps
    // playing on its own.
    for (const QPointer<QLabel>& t : stack_) {
      if (!t || !t->property(kLeavingProperty).toBool()
             || t->property(kTextProperty).toString() != text)
        continue;
      for (QPropertyAnimation* a : t->findChildren<QPropertyAnimation*>()) a->stop();
      stack_.removeAll(t);
      t->deleteLater();
      break;   // text is unique among live toasts, but a stale leaving one is a one-off
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
      themedIcon(QString::fromLatin1(glyph), QColor(Qt::white), 16, dpr)
          .pixmap(QSize(16, 16), dpr)
          .toImage()
          .save(&buf, "PNG");
    }
    // A one-row TABLE, not inline vertical-align on the <img>: Qt's rich text aligns an
    // inline image's "middle" to the font's own x-height rather than the true line
    // centre, which reads as the glyph riding high next to the text. A table cell's
    // vertical-align is a real box-centre, matching the browser's flex `align-items:
    // center`; the 8px cell padding mirrors its `gap: 8px` too.
    toast->setText(QString(
        "<table cellspacing=\"0\" cellpadding=\"0\" width=\"100%\"><tr>"
        "<td style=\"vertical-align: middle;\">"
        "<img src=\"data:image/png;base64,%1\" width=\"16\" height=\"16\"></td>"
        "<td style=\"vertical-align: middle; padding-left: 8px;\">%2</td>"
        "</tr></table>")
                       .arg(QString::fromLatin1(png.toBase64()), text.toHtmlEscaped()));
    // The message on its own — text() is now markup wrapping an inline glyph, so anything
    // reading a toast back (the GUI tests) has a plain string to compare.
    toast->setProperty(kTextProperty, text);
    toast->setObjectName("toast");
    // The browser's inner box (.notify-toast: 12px beside the content, 10px above and
    // below) less the 4px margin the rich-text document adds on every side itself.
    toast->setStyleSheet(QString("QLabel#toast { background: %1; color: white; "
                                 "padding: 6px 8px; border-radius: 6px; }")
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
    fx->setOpacity(1.0);   // real appearance first — the dust flight grabs this snapshot
    toast->setGraphicsEffect(fx);
    toast->show();
    toast->raise();
    stack_.push_back(toast);
    reflow();
    const QRect rest = toast->geometry();
    // Sand first; a decline falls back to the plain fade + rise-into-place below.
    if (auto* overlay = dustToastIn(toast, fx, host_, rest, leftInset_)) {
      entering_[toast] = overlay;
    } else {
      fx->setOpacity(0.0);
      auto* fadeIn = new QPropertyAnimation(fx, "opacity", toast);
      fadeIn->setDuration(kFadeInMs);
      fadeIn->setStartValue(0.0);
      fadeIn->setEndValue(1.0);
      fadeIn->setEasingCurve(QEasingCurve::OutCubic);
      fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
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
    }

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
}


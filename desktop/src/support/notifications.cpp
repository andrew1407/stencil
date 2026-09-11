#include "notifications.hpp"
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

namespace {
  // Fade durations for the toast lifecycle. Opacity is animated via
  // QGraphicsOpacityEffect on the label — purely Qt-core, so it renders
  // identically on macOS/Windows/Linux with no compositor dependency.
  constexpr int kFadeInMs = 180;
  constexpr int kFadeOutMs = 160;
  // How far a toast rises in from / drops away to (browser: notifyLeave's 14px) — the
  // plain fade+rise fallback only.
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

  // Toast dust (browser motion.js surfaceIn/surfaceOut; desktop DisintegrateOverlay).
  // 2x the shared menu clock — a passing notice can afford to drift rather than snap.
  // Leaving is slower still than arriving (browser ENTER_DUST_MS / LEAVE_DUST_MS).
  constexpr int kToastInMs = 560;   // browser ENTER_DUST_MS 840 / 1.5
  // Shorter than the entrance, not longer: an arrival can afford to drift, a departure has
  // nothing left to look at. Browser twin: js/ui/notifications.js LEAVE_DUST_MS.
  constexpr int kToastOutMs = 420;


  // Past the FREE area's left edge (`freeLeft`: the window's, or a left-docked chat's
  // inner edge), at the toast's own height. Only 0.15 toast-widths past it, not a docked
  // panel's 1.2: the stack already sits at that edge, so a farther point had every grain
  // off screen within 150ms and the exit read as a cut (browser notifications.js twin).
  constexpr double kToastReach = 0.15;
  QPoint toastDustPoint(const QRect& r, int freeLeft) {
    return QPoint(freeLeft - qRound(r.width() * kToastReach), r.center().y());
  }

  // Room between a left-docked chat and the stack beside it.
  constexpr int kDockGapPx = 8;

  // Keep a toast's cloud on the free side of a left-docked chat (`freeLeft` = the
  // panel's width, 0 without one): the point it flies to/from is behind the panel, and
  // unclipped the motes streamed across the composer (browser clipDustToFree twin).
  void clipToFree(stencil::gui::DisintegrateOverlay* overlay, QWidget* host, int freeLeft) {
    if (overlay && freeLeft > 0)
      overlay->setPaintClip(QRect(freeLeft, 0, host->width() - freeLeft, host->height()));
  }

  // Returns the flying cloud (so the caller can track it for reflow()'s retarget), or
  // null — declined under STENCIL_NO_ANIM / offscreen.
  stencil::gui::DisintegrateOverlay* dustToastIn(QLabel* toast, QGraphicsOpacityEffect* fx,
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
  bool dustToastOut(QLabel* toast, QWidget* host, int freeLeft) {
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

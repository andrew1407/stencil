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

  // Middle-ellipsize any whitespace-free run longer than 48 chars (browser squeezeLongTokens parity).
  static QString squeezeLongTokens(const QString& text) {
    constexpr int MAX_TOKEN_CHARS = 48;
    const QStringList parts = text.split(QLatin1Char(' '));
    QStringList out;
    out.reserve(parts.size());
    for (const QString& tok : parts) {
      if (tok.size() <= MAX_TOKEN_CHARS) { out << tok; continue; }
      const int keep = (MAX_TOKEN_CHARS - 1) / 2;
      out << tok.left(keep) + QChar(0x2026) + tok.right(keep);
    }
    return out.join(QLatin1Char(' '));
  }

  void Notifications::show(const QString& rawText, Level level, int msec) {
    if (!host_) return;
    const QString text = squeezeLongTokens(rawText);

    // Coalesce: an identical standing message just lives longer (browser parity).
    for (QLabel* t : liveToasts())
      if (t->property(TEXT_PROPERTY).toString() == text) {
        if (auto* life = t->findChild<QTimer*>("toastLife")) life->start(msec);
        return;
      }

    // A repeat landing while the LAST one is leaving finishes that label outright; its
    // dust cloud is a separate overlay and keeps playing.
    for (const QPointer<QLabel>& t : stack_) {
      if (!t || !t->property(LEAVING_PROPERTY).toBool()
             || t->property(TEXT_PROPERTY).toString() != text)
        continue;
      for (QPropertyAnimation* a : t->findChildren<QPropertyAnimation*>()) a->stop();
      stack_.removeAll(t);
      t->deleteLater();
      break;   // text is unique among live toasts, but a stale leaving one is a one-off
    }

    // Cap BEFORE adding; only standing toasts count.
    const QList<QLabel*> live = liveToasts();
    for (int i = 0; i < live.size() + 1 - MAX_VISIBLE && i < live.size(); ++i)
      dismiss(live[i]);

    // Browser toast variants: --danger for a failure, --accent for everything else.
    const QColor bg = level == Level::Error ? errorBg_ : normalBg_;
    const char* glyph = level == Level::Success ? "check" : level == Level::Error ? "x" : "info";

    auto* toast = new QLabel(host_);
    toast->setTextFormat(Qt::RichText);
    // Inline image so one widget carries the whole toast (this target links a minimal source set).
    QByteArray png;
    {
      QBuffer buf(&png);
      buf.open(QIODevice::WriteOnly);
      // The dpr-AWARE overload — pixmap(16, 16) would give a soft glyph on hi-dpi.
      const qreal dpr = qApp ? qApp->devicePixelRatio() : qreal(1);
      themedIcon(QString::fromLatin1(glyph), QColor(Qt::white), 16, dpr)
          .pixmap(QSize(16, 16), dpr)
          .toImage()
          .save(&buf, "PNG");
    }
    // A one-row TABLE: Qt aligns an inline image's "middle" to the x-height, not the line
    // centre. The 8px cell padding mirrors the browser's `gap: 8px`.
    toast->setText(QString(
        "<table cellspacing=\"0\" cellpadding=\"0\" width=\"100%\"><tr>"
        "<td style=\"vertical-align: middle;\">"
        "<img src=\"data:image/png;base64,%1\" width=\"16\" height=\"16\"></td>"
        "<td style=\"vertical-align: middle; padding-left: 8px;\">%2</td>"
        "</tr></table>")
                       .arg(QString::fromLatin1(png.toBase64()), text.toHtmlEscaped()));
    // The plain message, for anything reading a toast back (the GUI tests).
    toast->setProperty(TEXT_PROPERTY, text);
    toast->setObjectName("toast");
    // Browser .notify-toast padding less the 4px the rich-text document adds itself.
    toast->setStyleSheet(QString("QLabel#toast { background: %1; color: white; "
                                 "padding: 6px 8px; border-radius: 6px; }")
                             .arg(bg.name()));
    toast->setAttribute(Qt::WA_TransparentForMouseEvents);
    // Resolve the stylesheet before measuring, or adjustSize() sizes the unstyled label.
    toast->ensurePolished();
    toast->adjustSize();
    // Never wider than the browser toast cap (or the host).
    const int cap = qMin(380, qMax(120, host_->width() - 36));
    if (toast->width() > cap) {
      toast->setWordWrap(true);
      toast->setFixedWidth(cap);
      toast->adjustSize();
      toast->setFixedHeight(toast->heightForWidth(cap));
    }

    // The label owns the effect and the animation deletes itself: no lifetime bookkeeping.
    auto* fx = new QGraphicsOpacityEffect(toast);
    fx->setOpacity(1.0);   // real appearance first — the dust flight grabs this snapshot
    toast->setGraphicsEffect(fx);
    toast->show();
    toast->raise();
    stack_.push_back(toast);
    reflow();
    const QRect rest = toast->geometry();
    if (auto* overlay = dustToastIn(toast, fx, host_, rest, leftInset_)) {
      entering_[toast] = overlay;
    } else {
      fx->setOpacity(0.0);
      auto* fadeIn = new QPropertyAnimation(fx, "opacity", toast);
      fadeIn->setDuration(FADE_IN_MS);
      fadeIn->setStartValue(0.0);
      fadeIn->setEndValue(1.0);
      fadeIn->setEasingCurve(QEasingCurve::OutCubic);
      fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
      auto* riseIn = new QPropertyAnimation(toast, "geometry", toast);
      // Named so reflow() can retarget it: an entrance left pointing at the OLD slot would
      // drag a restacked toast back down there when it finished.
      riseIn->setObjectName("toastRise");
      riseIn->setDuration(FADE_IN_MS + 80);
      riseIn->setStartValue(rest.translated(0, SLIDE_PX));
      riseIn->setEndValue(rest);
      riseIn->setEasingCurve(QEasingCurve::OutBack);
      riseIn->start(QAbstractAnimation::DeleteWhenStopped);
    }

    // Named and restartable so the coalescing branch can extend a standing toast.
    auto* life = new QTimer(toast);
    life->setObjectName("toastLife");
    life->setSingleShot(true);
    connect(life, &QTimer::timeout, toast, [this, toast] { dismiss(toast); });
    life->start(msec);
  }

  // Oldest first; the ones already leaving are filtered out by LEAVING_PROPERTY.
  QList<QLabel*> Notifications::liveToasts() const {
    QList<QLabel*> live;
    for (const QPointer<QLabel>& t : stack_)
      if (t && !t->property(LEAVING_PROPERTY).toBool()) live.push_back(t.data());
    return live;
  }
}


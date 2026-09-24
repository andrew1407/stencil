#include "ToastStack.hpp"
#include "notificationsParts.hpp"
#include "DisintegrateOverlay.hpp"
#include "iconSet.hpp"
#include "modalReveal.hpp"  // stencil::support::motionReduced()
#include "ModalBackdrop.hpp"
#include "logoStageRules.hpp"
#include "skinPrefs.hpp"
#include "toastSkin.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSvgRenderer>
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

  namespace {
    // The mark a show's notice wears is taller than a 16px glyph, and svgArt.json crops the
    // viewBox to the shell, so it fills this box edge to edge.
    constexpr int EGG_W = 14, EGG_H = 18;

    // The egg a show's notice wears, sized and inked by the caller (the %1 / %2 / %3 placeholders).
    QString secretEggSvg(int w, int h, const QColor& ink) {
      static const QString art = [] {
        QFile f(":/config/svgArt.json");
        if (!f.open(QIODevice::ReadOnly)) return QString();
        return QJsonDocument::fromJson(f.readAll()).object().value("secretEgg").toString();
      }();
      return QString(art).replace(QLatin1String("%1"), QString::number(w))
                         .replace(QLatin1String("%2"), QString::number(h))
                         .replace(QLatin1String("%3"), ink.name());
    }

    // Middle-ellipsize any whitespace-free run longer than 48 chars (browser squeezeLongTokens parity).
    QString squeezeLongTokens(const QString& text) {
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
  }  // namespace

  void ToastStack::show(const QString& rawText, Level level, int msec, bool special) {
    if (!host) return;
    const QString text = squeezeLongTokens(rawText);

    // Coalesce: an identical standing message just lives longer (browser parity).
    for (QLabel* t : liveToasts())
      if (t->property(TEXT_PROPERTY).toString() == text) {
        if (auto* life = t->findChild<QTimer*>("toastLife")) life->start(msec);
        return;
      }

    // A repeat landing while the LAST one is leaving finishes that label outright; its
    // dust cloud is a separate overlay and keeps playing.
    for (const QPointer<QLabel>& t : stack) {
      if (!t || !t->property(LEAVING_PROPERTY).toBool()
             || t->property(TEXT_PROPERTY).toString() != text)
        continue;
      for (QPropertyAnimation* a : t->findChildren<QPropertyAnimation*>()) a->stop();
      stack.removeAll(t);
      t->deleteLater();
      break;   // text is unique among live toasts, but a stale leaving one is a one-off
    }

    // Cap BEFORE adding; only standing toasts count.
    const QList<QLabel*> live = liveToasts();
    for (int i = 0; i < live.size() + 1 - MAX_VISIBLE && i < live.size(); ++i)
      dismiss(live[i]);

    // Browser toast variants: --danger for a failure, --accent for everything else — and gold
    // for a logo show's own notice (browser .notify-shine). A skin's notice is a system message
    // box in its own face, the gold one included.
    const ToastDress dress = toastDress(special, level == Level::ERROR, normalBg, errorBg);
    const QColor& ink = dress.ink;
    const char* glyph = special ? "egg"
                       : level == Level::SUCCESS ? "check" : level == Level::ERROR ? "x" : "info";

    auto* toast = new QLabel(host);
    toast->setTextFormat(Qt::RichText);
    // Inline image so one widget carries the whole toast (this target links a minimal source set).
    QByteArray png;
    {
      QBuffer buf(&png);
      buf.open(QIODevice::WriteOnly);
      // The dpr-AWARE overload — pixmap(16, 16) would give a soft glyph on hi-dpi.
      const qreal dpr = qApp ? qApp->devicePixelRatio() : qreal(1);
      if (special) {
        // Not a glyph-table name: art of its own, rendered straight from the shared table.
        QImage art(QSize(EGG_W, EGG_H) * dpr, QImage::Format_ARGB32_Premultiplied);
        art.fill(Qt::transparent);
        art.setDevicePixelRatio(dpr);
        QPainter ap(&art);
        QSvgRenderer(secretEggSvg(EGG_W, EGG_H, ink).toUtf8()).render(&ap, QRectF(0, 0, EGG_W, EGG_H));
        ap.end();
        art.save(&buf, "PNG");
      } else {
        themedIcon(QString::fromLatin1(glyph), ink, 16, dpr)
            .pixmap(QSize(16, 16), dpr)
            .toImage()
            .save(&buf, "PNG");
      }
    }
    // A one-row TABLE: Qt aligns an inline image's "middle" to the x-height, not the line
    // centre. The 8px cell padding mirrors the browser's `gap: 8px`.
    toast->setText(QString(
        "<table cellspacing=\"0\" cellpadding=\"0\" width=\"100%\"><tr>"
        "<td style=\"vertical-align: middle;\">"
        "<img src=\"data:image/png;base64,%1\" width=\"%2\" height=\"%3\"></td>"
        "<td style=\"vertical-align: middle; padding-left: 8px;\">%4</td>"
        "</tr></table>")
                       .arg(QString::fromLatin1(png.toBase64()))
                       .arg(special ? EGG_W : 16)
                       .arg(special ? EGG_H : 16)
                       .arg(text.toHtmlEscaped()));
    // The plain message, for anything reading a toast back (the GUI tests).
    toast->setProperty(TEXT_PROPERTY, text);
    toast->setProperty(LEVEL_PROPERTY, static_cast<int>(level));
    toast->setObjectName("toast");
    // A toast outranks an open dialog's backdrop, as #notify-balloon outranks .app-modal-overlay.
    toast->setProperty(support::ModalBackdrop::ABOVE_PROPERTY, true);
    toast->setStyleSheet(toastSheet(dress, special));
    toast->setAttribute(Qt::WA_TransparentForMouseEvents);
    // Resolve the stylesheet before measuring, or adjustSize() sizes the unstyled label.
    toast->ensurePolished();
    toast->adjustSize();
    // Never wider than the browser toast cap (or the host).
    const int cap = qMin(380, qMax(120, host->width() - 36));
    if (toast->width() > cap) {
      toast->setWordWrap(true);
      toast->setFixedWidth(cap);
      toast->adjustSize();
      toast->setFixedHeight(toast->heightForWidth(cap));
    }
    if (dress.skinned) new ToastTitleStrip(toast, dress.titleA, dress.titleB);

    // The label owns the effect and the animation deletes itself: no lifetime bookkeeping.
    auto* fx = new QGraphicsOpacityEffect(toast);
    fx->setOpacity(1.0);   // real appearance first — the dust flight grabs this snapshot
    toast->setGraphicsEffect(fx);
    toast->show();
    toast->raise();
    stack.push_back(toast);
    reflow();
    const QRect rest = toast->geometry();
    if (auto* overlay = dustToastIn(toast, fx, host, rest, leftInset)) {
      entering[toast] = overlay;
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
  QList<QLabel*> ToastStack::liveToasts() const {
    QList<QLabel*> live;
    for (const QPointer<QLabel>& t : stack)
      if (t && !t->property(LEAVING_PROPERTY).toBool()) live.push_back(t.data());
    return live;
  }
}


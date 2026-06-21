#include "incognitoOverlay.hpp"
#include "theme.hpp"

#include <QColor>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QRectF>

namespace stencil::gui {

  IncognitoOverlay::IncognitoOverlay(QWidget* viewport) : QWidget(viewport) {
    // Click-through and non-focusable: the canvas underneath stays fully
    // interactive. WA_NoSystemBackground + a translucent background let the
    // canvas show through everywhere we don't paint the outline/badge.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    if (viewport) viewport->installEventFilter(this);
    fitToParent();
    hide();
  }

  void IncognitoOverlay::setActive(bool on) {
    if (active_ == on) return;
    active_ = on;
    if (on) {
      fitToParent();
      raise();  // stay above the canvas sibling
    }
    setVisible(on);
    update();
  }

  void IncognitoOverlay::setTheme(bool dark, const QString& accentKey) {
    dark_ = dark;
    accentKey_ = accentKey;
    if (active_) update();
  }

  void IncognitoOverlay::fitToParent() {
    if (parentWidget()) setGeometry(parentWidget()->rect());
  }

  bool IncognitoOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
      fitToParent();
      if (active_) { raise(); update(); }
    }
    return QWidget::eventFilter(watched, event);
  }

  void IncognitoOverlay::paintEvent(QPaintEvent*) {
    if (!active_) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor accent = themePalette(dark_, accentKey_).accent;

    // 3px dashed accent outline, inset by 3px — mirrors
    //   body.incognito-mode .canvas-viewport { outline: 3px dashed var(--accent);
    //                                           outline-offset: -3px; }
    QPen pen(accent);
    pen.setStyle(Qt::DashLine);
    pen.setWidth(3);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(rect()).adjusted(3, 3, -3, -3));

    // "🕶 Incognito — not saved" pill at top-left (8,8) — mirrors
    //   body.incognito-mode .canvas-section::after { ... background: accent 90%;
    //     color: #fff; font: 600 11px; padding: 3px 9px; border-radius: 10px; }
    QFont f = font();
    f.setPixelSize(11);
    f.setWeight(QFont::DemiBold);
    const QString label = QStringLiteral("🕶 Incognito — not saved");
    const QFontMetrics fm(f);
    const qreal padX = 9.0, padY = 3.0, radius = 10.0;
    const QRectF pill(8, 8, fm.horizontalAdvance(label) + padX * 2,
                      fm.height() + padY * 2);
    QColor bg = accent;
    bg.setAlphaF(0.9);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(pill, radius, radius);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(pill, Qt::AlignCenter, label);
  }

}

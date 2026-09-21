#include "IncognitoOverlay.hpp"
#include "theme.hpp"

#include <QColor>
#include <QEvent>
#include <QPainter>
#include <QRectF>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

namespace stencil::gui {

  IncognitoOverlay::IncognitoOverlay(QWidget* viewport) : QWidget(viewport) {
    // Click-through, non-focusable; WA_NoSystemBackground lets the canvas show through.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    if (viewport) viewport->installEventFilter(this);
    fitToParent();
    hide();
  }

  // Four equal quarters from the top-left, each a straight run so `t` maps linearly (browser .ig-edge).
  QPainterPath IncognitoOverlay::framePath(const QRectF& box, double t) {
    QPainterPath path;
    t = std::clamp(t, 0.0, 1.0);
    if (t <= 0.0 || box.isEmpty()) return path;
    const auto run = [t](int q) { return std::clamp(t * 4.0 - q, 0.0, 1.0); };
    const qreal w = box.width(), h = box.height();
    path.moveTo(box.topLeft());
    path.lineTo(box.left() + w * run(0), box.top());
    if (t > 0.25) { path.moveTo(box.topRight());    path.lineTo(box.right(), box.top() + h * run(1)); }
    if (t > 0.50) { path.moveTo(box.bottomRight()); path.lineTo(box.right() - w * run(2), box.bottom()); }
    if (t > 0.75) { path.moveTo(box.bottomLeft());  path.lineTo(box.left(), box.bottom() - h * run(3)); }
    return path;
  }

  void IncognitoOverlay::setActive(bool on) {
    if (active == on) return;
    active = on;
    if (on) {
      fitToParent();
      raise();  // stay above the canvas sibling
      show();
    }
    if (!anim) {
      anim = new QVariantAnimation(this);
      anim->setEasingCurve(QEasingCurve::OutCubic);
      connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        progress = v.toDouble();
        update();
      });
      // Hide only once the frame has finished retracting.
      connect(anim, &QVariantAnimation::finished, this, [this] {
        if (!active) hide();
      });
    }
    // Capture the frame's position BEFORE touching the animation: setStartValue/setEndValue emit
    // valueChanged into progress, so reading it after them yields the END value (an instant snap).
    const double from = progress;
    const double to = on ? 1.0 : 0.0;
    const int ms = std::max(1, int(DRAW_MS * std::abs(to - from)));
    anim->stop();
    anim->setDuration(ms);
    anim->setStartValue(from);
    anim->setEndValue(to);
    anim->start();
    update();
  }

  void IncognitoOverlay::setTheme(bool dark, const QString& accentKey) {
    this->dark = dark;
    this->accentKey = accentKey;
    if (active) update();
  }

  void IncognitoOverlay::fitToParent() {
    if (parentWidget()) setGeometry(parentWidget()->rect());
  }

  bool IncognitoOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
      fitToParent();
      if (active) { raise(); update(); }
    }
    return QWidget::eventFilter(watched, event);
  }

  void IncognitoOverlay::paintEvent(QPaintEvent*) {
    if (progress <= 0.0) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor accent = themePalette(dark, accentKey).accent;

    // Browser: 3px dashed outline, outline-offset -3px. Qt strokes centred, so the only inset is
    // PEN_PX/2. The dash pattern rides ON the partial path, so dashes are REVEALED, not stretched.
    QPen pen(accent);
    pen.setStyle(Qt::DashLine);
    pen.setWidth(PEN_PX);
    pen.setCapStyle(Qt::FlatCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(framePath(frameBox(QRectF(rect())), progress));
  }

}

#include "IncognitoOverlay.hpp"
#include "motionPrefs.hpp"
#include "theme.hpp"

#include <QColor>
#include <QEasingCurve>
#include <QEvent>
#include <QPainter>
#include <QRectF>
#include <QVariantAnimation>

#include <algorithm>

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

  IncognitoOverlay::EdgeLengths IncognitoOverlay::edgeLengths(double ms, bool drawing,
                                                             const EdgeLengths& from) {
    const QEasingCurve ease(QEasingCurve::OutCubic);   // browser: ease-out
    const double to = drawing ? 1.0 : 0.0;
    EdgeLengths out{};
    for (int e = 0; e < 4; ++e) {
      // ON runs top, right, bottom, left; OFF mirrors the delays, so the last edge drawn goes first.
      const int slot = drawing ? e : 3 - e;
      const double u = std::clamp((ms - slot * STAGGER_MS) / EDGE_MS, 0.0, 1.0);
      out[e] = from[e] + (to - from[e]) * ease.valueForProgress(u);
    }
    return out;
  }

  QPainterPath IncognitoOverlay::framePath(const QRectF& box, const EdgeLengths& len) {
    QPainterPath path;
    if (box.isEmpty()) return path;
    const qreal w = box.width(), h = box.height();
    const auto run = [&len](int e) { return std::clamp(len[e], 0.0, 1.0); };
    // One run per edge from the corner it starts at, never a growing perimeter: length is what
    // animates, so the dash pattern is REVEALED along each axis instead of smeared.
    if (run(0) > 0.0) { path.moveTo(box.topLeft());     path.lineTo(box.left() + w * run(0), box.top()); }
    if (run(1) > 0.0) { path.moveTo(box.topRight());    path.lineTo(box.right(), box.top() + h * run(1)); }
    if (run(2) > 0.0) { path.moveTo(box.bottomRight()); path.lineTo(box.right() - w * run(2), box.bottom()); }
    if (run(3) > 0.0) { path.moveTo(box.bottomLeft());  path.lineTo(box.left(), box.bottom() - h * run(3)); }
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
      anim->setDuration(DRAW_MS);
      anim->setStartValue(0.0);
      anim->setEndValue(double(DRAW_MS));   // linear: each edge carries its own easing
      connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        edges = edgeLengths(v.toDouble(), active, from);
        update();
      });
      // Hide only once the frame has finished retracting.
      connect(anim, &QVariantAnimation::finished, this, [this] {
        if (!active) hide();
      });
    }
    anim->stop();
    from = edges;   // a flip mid-flight carries on from where each edge stands
    if (support::motionReduced()) {
      edges.fill(on ? 1.0 : 0.0);
      if (!on) hide();
      update();
      return;
    }
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
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor accent = themePalette(dark, accentKey).accent;

    // Browser: a 3px dashed frame, accent 0 9px / transparent 9px 16px. Qt's dash pattern is in
    // PEN WIDTHS, so the browser's pixel run divides by the width.
    QPen pen(accent);
    pen.setWidth(PEN_PX);
    pen.setStyle(Qt::CustomDashLine);
    pen.setDashPattern({DASH_ON_PX / PEN_PX, DASH_OFF_PX / PEN_PX});
    pen.setCapStyle(Qt::FlatCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(framePath(frameBox(QRectF(rect())), edges));
  }

}

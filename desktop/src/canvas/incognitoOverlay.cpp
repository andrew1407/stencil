#include "incognitoOverlay.hpp"
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

  // The clockwise perimeter, revealed in four equal quarters starting at the top-left:
  // top → right → bottom → left. Each quarter is a straight run, so `t` maps linearly
  // onto it. Port of the four staggered .ig-edge elements in the browser, and the reason
  // both surfaces draw the dashes in the same direction rather than just fading a box in.
  QPainterPath IncognitoOverlay::framePath(const QRectF& box, double t) {
    QPainterPath path;
    t = std::clamp(t, 0.0, 1.0);
    if (t <= 0.0 || box.isEmpty()) return path;
    // Quarter q spans [q/4, (q+1)/4); `run` is how far into the current quarter we are.
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
    if (active_ == on) return;
    active_ = on;
    if (on) {
      fitToParent();
      raise();  // stay above the canvas sibling
      show();
    }
    if (!anim_) {
      anim_ = new QVariantAnimation(this);
      anim_->setEasingCurve(QEasingCurve::OutCubic);
      connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        progress_ = v.toDouble();
        update();
      });
      // Only hide once the frame has finished retracting — hiding on the toggle would
      // cut the animation off at its first frame.
      connect(anim_, &QVariantAnimation::finished, this, [this] {
        if (!active_) hide();
      });
    }
    // Capture where the frame actually is BEFORE touching the animation. Both
    // setStartValue and setEndValue recalculate the current interval and emit
    // valueChanged, which lands right back in progress_ via the connection below — so
    // reading progress_ after them yields the new END value, and the duration below
    // would come out as "no distance to cover", i.e. an instant snap on every toggle
    // after the first.
    const double from = progress_;
    const double to = on ? 1.0 : 0.0;
    // Scale the time to the distance left, so a fast re-toggle doesn't crawl.
    const int ms = std::max(1, int(kDrawMs * std::abs(to - from)));
    anim_->stop();
    anim_->setDuration(ms);
    anim_->setStartValue(from);
    anim_->setEndValue(to);
    anim_->start();
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
    if (progress_ <= 0.0) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor accent = themePalette(dark_, accentKey_).accent;

    // 3px dashed accent outline sitting FLUSH on the viewport edge — mirrors
    //   body.incognito-mode .canvas-viewport { outline: 3px dashed var(--accent);
    //                                           outline-offset: -3px; }
    // which puts the outline's OUTER edge on the box. Qt strokes centred, so the
    // only inset is the pen's half-width (kPenPx/2) — anything more leaves a gap
    // of bare canvas outside the dashes.
    // Stroke only the part of the perimeter drawn so far. The dash pattern rides ON the
    // partial path, so the dashes are REVEALED in order rather than stretched into place.
    QPen pen(accent);
    pen.setStyle(Qt::DashLine);
    pen.setWidth(kPenPx);
    pen.setCapStyle(Qt::FlatCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(framePath(frameBox(QRectF(rect())), progress_));
    // No badge over the picture: the "Incognito — not saved" fact lives on the
    // toolbar's "?" hint beside the project name, where it covers no content.
  }

}

#pragma once
// iconMotion.json `extras.swap-click-turn`: one clockwise revolution of a control's glyph
// in answer to a CLICK — the browser twin is css/animations/iconClick.css, and the timing
// is rotate-cw's, so every spin in the app reads the same. Qt has no CSS animation, so
// each frame re-renders the pixmap the button already wears, turned about its own centre
// into a box of the SAME size (QPixmap::transformed would grow it and the glyph would
// appear to pulse). Q_OBJECT-free so a test target needs no extra source.
#include "motionPrefs.hpp"

#include <QAbstractButton>
#include <QEasingCurve>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QVariantAnimation>

namespace stencil::support {

  inline constexpr int ICON_TURN_MS = 413;   // iconMotion.json rotate-cw / swap-click-turn
  inline constexpr const char* SPINNING_PROPERTY = "stencilIconSpinning";

  inline void spinIconOnce(QAbstractButton* btn) {
    if (!btn || motionReduced() || btn->property(SPINNING_PROPERTY).toBool()) return;
    const QPixmap base = btn->icon().pixmap(btn->iconSize() * btn->devicePixelRatioF());
    if (base.isNull()) return;
    const QIcon rest = btn->icon();   // put back exactly what it wore, themed and all
    btn->setProperty(SPINNING_PROPERTY, true);
    auto* turn = new QVariantAnimation(btn);
    turn->setDuration(ICON_TURN_MS);
    turn->setStartValue(0.0);
    turn->setEndValue(360.0);
    turn->setEasingCurve(QEasingCurve::OutCubic);
    QPointer<QAbstractButton> guard(btn);
    QObject::connect(turn, &QVariantAnimation::valueChanged, btn, [guard, base](const QVariant& v) {
      if (!guard) return;
      const qreal dpr = base.devicePixelRatio() > 0 ? base.devicePixelRatio() : 1.0;
      QPixmap frame(base.size());
      frame.setDevicePixelRatio(dpr);
      frame.fill(Qt::transparent);
      QPainter p(&frame);
      p.setRenderHint(QPainter::SmoothPixmapTransform);
      const QPointF c(base.width() / dpr / 2.0, base.height() / dpr / 2.0);
      p.translate(c);
      p.rotate(v.toDouble());
      p.translate(-c);
      p.drawPixmap(0, 0, base);
      p.end();
      guard->setIcon(QIcon(frame));
    });
    QObject::connect(turn, &QVariantAnimation::finished, btn, [guard, rest] {
      if (!guard) return;
      guard->setIcon(rest);
      guard->setProperty(SPINNING_PROPERTY, false);
    });
    turn->start(QAbstractAnimation::DeleteWhenStopped);
  }

}  // namespace stencil::support

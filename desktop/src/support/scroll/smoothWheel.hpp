#pragma once
// A list scrolled as Chrome scrolls an overflow:auto box (browser #projects-list): a trackpad moves
// it by exactly the pixels it reports, its momentum included, and a wheel notch eases WHEEL_NOTCH_PX.
#include "../motionPrefs.hpp"

#include <QAbstractScrollArea>
#include <QEasingCurve>
#include <QEvent>
#include <QScrollBar>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <algorithm>

namespace stencil::support {

  inline constexpr int WHEEL_NOTCH_PX = 100;   // Chrome's distance per notch (120 eighths of a degree)
  inline constexpr int WHEEL_EASE_MS = 150;

  class SmoothWheel : public QObject {
   public:
    explicit SmoothWheel(QAbstractScrollArea* area) : QObject(area), area(area) {
      ease.setEasingCurve(QEasingCurve::OutCubic);
      connect(&ease, &QVariantAnimation::valueChanged, this,
              [this](const QVariant& v) { this->area->verticalScrollBar()->setValue(v.toInt()); });
      area->viewport()->installEventFilter(this);
    }

    bool eventFilter(QObject* watched, QEvent* e) override {
      if (e->type() != QEvent::Wheel) return QObject::eventFilter(watched, e);
      const auto* w = static_cast<QWheelEvent*>(e);
      QScrollBar* bar = area->verticalScrollBar();
      // A zoom chord, a sideways scroll or a list with nothing to scroll keeps Qt's own handling.
      if ((w->modifiers() & Qt::ControlModifier) || bar->maximum() <= bar->minimum()) return false;
      if (!w->pixelDelta().isNull()) {
        if (!w->pixelDelta().y()) return false;
        ease.stop();
        bar->setValue(bar->value() - w->pixelDelta().y());
        return true;
      }
      const int dy = w->angleDelta().y();
      if (!dy) return false;
      // Notches in quick succession add up, each from where the last was heading.
      const int from = ease.state() == QAbstractAnimation::Running ? target : bar->value();
      target = std::clamp(from - dy * WHEEL_NOTCH_PX / 120, bar->minimum(), bar->maximum());
      ease.stop();
      if (motionReduced()) { bar->setValue(target); return true; }
      ease.setStartValue(bar->value());
      ease.setEndValue(target);
      ease.setDuration(motionMs(WHEEL_EASE_MS));
      ease.start();
      return true;
    }

    int heading() const { return target; }   // where an easing notch lands

   private:
    QAbstractScrollArea* area;
    QVariantAnimation ease;
    int target = 0;
  };

}  // namespace stencil::support

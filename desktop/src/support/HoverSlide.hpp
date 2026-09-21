#pragma once
// Hover slide — port of the browser's `.accent-dd-opt:hover { transform: translateX(2px) }`.
// Qt style sheets animate nothing, so the row's own geometry moves; a re-layout
// underneath is taken as its new resting place, not fought. Q_OBJECT-free, no MOC.
#include "motionPrefs.hpp"   // support::motionReduced()

#include <QEasingCurve>
#include <QEvent>
#include <QObject>
#include <QVariantAnimation>
#include <QWidget>

#include <cmath>

namespace stencil::gui {

  class HoverSlide : public QObject {
   public:
    // browser numbers: translateX(2px) over `transform 0.12s ease`.
    explicit HoverSlide(QWidget* target, int px = 2, int ms = 120)
        : QObject(target), target(target), px(px), ms(ms) {
      anim.setEasingCurve(QEasingCurve::InOutQuad);   // CSS `ease`
      QObject::connect(&anim, &QVariantAnimation::valueChanged, this,
                       [this](const QVariant& v) { applyAt(v.toDouble()); });
      target->installEventFilter(this);
    }

   protected:
    bool eventFilter(QObject* o, QEvent* e) override {
      if (o == target) {
        switch (e->type()) {
          case QEvent::Enter: slideTo(1.0); break;
          case QEvent::Leave: slideTo(0.0); break;
          // A layout move (theme re-polish, resize) is the new resting x, not a slide of ours.
          case QEvent::Move: if (!moving) reanchor(); break;
          case QEvent::Hide: anim.stop(); at = 0.0; break;
          default: break;
        }
      }
      return QObject::eventFilter(o, e);
    }

   private:
    int offset() const { return int(std::lround(px * at)); }

    void slideTo(double to) {
      if (at == 0.0 && !anim.state()) baseX = target->x();   // its resting place
      anim.stop();
      if (support::motionReduced()) { applyAt(to); return; }
      anim.setStartValue(at);
      anim.setEndValue(to);
      anim.setDuration(int(ms * std::fabs(to - at)));
      anim.start();
    }

    void applyAt(double v) {
      at = v;
      moving = true;
      target->move(baseX + offset(), target->y());
      moving = false;
    }

    void reanchor() {
      baseX = target->x();
      if (at != 0.0) applyAt(at);
    }

    QWidget* target;
    int px;
    int ms;
    int baseX = 0;
    double at = 0.0;    // 0 = resting, 1 = fully slid
    bool moving = false;   // our own move(), not the layout's
    QVariantAnimation anim;
  };

  inline void installHoverSlide(QWidget* target) {
    if (target && !target->property("_hoverSlide").toBool()) {
      target->setProperty("_hoverSlide", true);   // guard against double-install
      new HoverSlide(target);                      // parented to target
    }
  }

}  // namespace stencil::gui

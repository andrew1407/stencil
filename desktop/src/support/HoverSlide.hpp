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
        : QObject(target), target_(target), px_(px), ms_(ms) {
      anim_.setEasingCurve(QEasingCurve::InOutQuad);   // CSS `ease`
      QObject::connect(&anim_, &QVariantAnimation::valueChanged, this,
                       [this](const QVariant& v) { applyAt(v.toDouble()); });
      target->installEventFilter(this);
    }

   protected:
    bool eventFilter(QObject* o, QEvent* e) override {
      if (o == target_) {
        switch (e->type()) {
          case QEvent::Enter: slideTo(1.0); break;
          case QEvent::Leave: slideTo(0.0); break;
          // A layout move (theme re-polish, resize) is the new resting x, not a slide of ours.
          case QEvent::Move: if (!moving_) reanchor(); break;
          case QEvent::Hide: anim_.stop(); at_ = 0.0; break;
          default: break;
        }
      }
      return QObject::eventFilter(o, e);
    }

   private:
    int offset() const { return int(std::lround(px_ * at_)); }

    void slideTo(double to) {
      if (at_ == 0.0 && !anim_.state()) baseX_ = target_->x();   // its resting place
      anim_.stop();
      if (support::motionReduced()) { applyAt(to); return; }
      anim_.setStartValue(at_);
      anim_.setEndValue(to);
      anim_.setDuration(int(ms_ * std::fabs(to - at_)));
      anim_.start();
    }

    void applyAt(double v) {
      at_ = v;
      moving_ = true;
      target_->move(baseX_ + offset(), target_->y());
      moving_ = false;
    }

    void reanchor() {
      baseX_ = target_->x();
      if (at_ != 0.0) applyAt(at_);
    }

    QWidget* target_;
    int px_;
    int ms_;
    int baseX_ = 0;
    double at_ = 0.0;    // 0 = resting, 1 = fully slid
    bool moving_ = false;   // our own move(), not the layout's
    QVariantAnimation anim_;
  };

  inline void installHoverSlide(QWidget* target) {
    if (target && !target->property("_hoverSlide").toBool()) {
      target->setProperty("_hoverSlide", true);   // guard against double-install
      new HoverSlide(target);                      // parented to target
    }
  }

}  // namespace stencil::gui

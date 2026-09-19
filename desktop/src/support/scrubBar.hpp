#pragma once
// A video scrub bar: the player's own progress line, shared by any surface that picks a
// frame. Browser twin: .oi-scrub (css/components/openImage.css) over the same tokens; the
// paint itself is the QSS rule for #oiFrameScrub in resources/app.qss.
#include <QEvent>
#include <QMouseEvent>
#include <QObject>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>

#include <algorithm>

namespace stencil::gui {

  // A player's bar seeks WHERE YOU TAP; a plain QSlider steps one page. SH_Slider_AbsoluteSetButtons
  // says so, but a per-widget QStyle drops the app stylesheet - so the press is mapped here.
  class ScrubClick : public QObject {
   public:
    using QObject::QObject;

   protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
      auto* s = qobject_cast<QSlider*>(obj);
      if (s && ev->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::LeftButton) {
          // The travel is the groove less the handle, which straddles the value it marks.
          const int handle = s->style()->pixelMetric(QStyle::PM_SliderLength, nullptr, s);
          const int span = std::max(1, s->width() - handle);
          const int x = int(me->position().x()) - handle / 2;
          s->setValue(QStyle::sliderValueFromPosition(s->minimum(), s->maximum(), x, span));
        }
      }
      return QObject::eventFilter(obj, ev);
    }
  };

  // The frame scrub as a player's progress bar: named for the QSS that paints it
  // (#oiFrameScrub), a pointer cursor QSS cannot give it, and tap-to-seek.
  inline void makeScrubBar(QSlider* s) {
    s->setObjectName(QStringLiteral("oiFrameScrub"));
    s->setCursor(Qt::PointingHandCursor);
    s->installEventFilter(new ScrubClick(s));
  }

}  // namespace stencil::gui

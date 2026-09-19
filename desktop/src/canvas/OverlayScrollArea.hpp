#pragma once
// True overlay scrollbars for the canvas viewport; browser parity: js/ui/canvasScrollbars.js.
// theme.cpp's QScrollBar stylesheet turns off Qt's native transient mode app-wide, and
// QAbstractScrollArea then shrinks the viewport to make room on every re-layout. So the base
// class's own bars are switched off for good - they stay the scroll MODEL that wheel/keyboard/
// setValue drive - and two plain QScrollBars owned here mirror them over the full viewport.
#include "PillScrollBars.hpp"  // ScrollBarPill: the painted thumb

#include <QEvent>
#include <QScrollArea>
#include <QScrollBar>

namespace stencil::gui {

  class OverlayScrollArea : public QScrollArea {
   public:
    explicit OverlayScrollArea(QWidget* parent = nullptr) : QScrollArea(parent) {
      setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
      setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
      vBar_ = new QScrollBar(Qt::Vertical, this);
      hBar_ = new QScrollBar(Qt::Horizontal, this);
      // Painted as pills (thin grey thumb, accent + a swell under the pointer) like every
      // other bar in the app — support/PillScrollBars.hpp; the theme feeds the colours.
      ScrollBarPill::adopt(vBar_);
      ScrollBarPill::adopt(hBar_);
      mirror(verticalScrollBar(), vBar_);
      mirror(horizontalScrollBar(), hBar_);
    }

    // The visible, floating bar for an axis. horizontalScrollBar()/verticalScrollBar() stay
    // the model (value/range); these are what the user hovers and drags.
    QScrollBar* overlayBar(Qt::Orientation o) const { return o == Qt::Vertical ? vBar_ : hBar_; }

    // Recomputes bar visibility/geometry. Range changes and area resizes call it themselves;
    // MainWindow also calls it after a zoom (revealCanvasScrollbars) as a backstop.
    void relayout() {
      if (!viewport()) return;
      const QRect vp = viewport()->geometry();
      const bool showV = verticalScrollBar()->maximum() > verticalScrollBar()->minimum();
      const bool showH = horizontalScrollBar()->maximum() > horizontalScrollBar()->minimum();
      const int vw = vBar_->sizeHint().width();
      const int hh = hBar_->sizeHint().height();
      // Each bar stops short of the other's corner, as the browser's do.
      vBar_->setGeometry(vp.right() - vw + 1, vp.top(), vw, vp.height() - (showH ? hh : 0));
      hBar_->setGeometry(vp.left(), vp.bottom() - hh + 1, vp.width() - (showV ? vw : 0), hh);
      vBar_->setVisible(showV);
      hBar_->setVisible(showH);
      vBar_->raise();
      hBar_->raise();
    }

   protected:
    bool event(QEvent* e) override {
      const bool handled = QScrollArea::event(e);
      if (e->type() == QEvent::Resize || e->type() == QEvent::LayoutRequest ||
          e->type() == QEvent::Show)
        relayout();
      return handled;
    }

   private:
    // Two-way value sync; range/steps flow model → view only. A mirrored setValue of an
    // unchanged value emits nothing, so the pair can't ping-pong.
    void mirror(QScrollBar* model, QScrollBar* view) {
      auto copyRange = [this, model, view] {
        view->setRange(model->minimum(), model->maximum());
        view->setPageStep(model->pageStep());
        view->setSingleStep(model->singleStep());
        view->setValue(model->value());
        relayout();
      };
      copyRange();
      connect(model, &QScrollBar::rangeChanged, view, copyRange);
      connect(model, &QScrollBar::valueChanged, view, &QScrollBar::setValue);
      connect(view, &QScrollBar::valueChanged, model, &QScrollBar::setValue);
    }

    QScrollBar* vBar_ = nullptr;
    QScrollBar* hBar_ = nullptr;
  };

}  // namespace stencil::gui

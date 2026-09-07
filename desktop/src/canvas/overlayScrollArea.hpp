#pragma once
// True overlay scrollbars for the canvas viewport.
// Browser parity: js/ui/canvasScrollbars.js draws the same two overlay bars over
// the canvas viewport with zero reserved layout space. Qt's native transient mode
// matches that, but theme.cpp's QScrollBar stylesheet turns it off app-wide,
// and QAbstractScrollArea then shrinks the viewport to make room for its bars
// every time it re-lays itself out (range change, resize, show). So the base
// class's own bars are switched off for good — they stay the scroll MODEL that
// wheel/keyboard/setValue drive — and two plain QScrollBars owned here mirror
// them and float over the full viewport.
#include "modalReveal.hpp"  // motionReduced()

#include <QEasingCurve>
#include <QEnterEvent>
#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QVariantAnimation>

namespace stencil::gui {

  // The overlay thumb, painted by hand: a fully rounded pill in the browser's thumb grey
  // that is thin at rest and swells a little under the pointer (layout.css: the thumb
  // grows into its slot on hover). The stylesheet still sizes the bar (theme.cpp
  // QScrollBar rules) and the hover shade is ours too — QSS on macOS draws the handle
  // square whatever radius it is given.
  class PillScrollBar : public QScrollBar {
   public:
    explicit PillScrollBar(Qt::Orientation o, QWidget* parent = nullptr) : QScrollBar(o, parent) {
      swell_ = new QVariantAnimation(this);
      swell_->setDuration(150);
      swell_->setEasingCurve(QEasingCurve::OutCubic);
      connect(swell_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        thick_ = v.toReal();
        update();
      });
    }
    void setThumbColors(const QColor& thumb, const QColor& hover) {
      thumb_ = thumb;
      hover_ = hover;
      update();
    }
    static constexpr qreal kRestThick = 6;    // thinner than the browser's 8 at rest…
    static constexpr qreal kHoverThick = 9;   // …and 1.5× under the pointer, as there

   protected:
    void enterEvent(QEnterEvent* e) override { swellTo(kHoverThick); QScrollBar::enterEvent(e); }
    void leaveEvent(QEvent* e) override { swellTo(kRestThick); QScrollBar::leaveEvent(e); }

    void paintEvent(QPaintEvent* e) override {
      if (!thumb_.isValid()) { QScrollBar::paintEvent(e); return; }
      QStyleOptionSlider opt;
      initStyleOption(&opt);
      const QRect slider = style()->subControlRect(QStyle::CC_ScrollBar, &opt,
                                                   QStyle::SC_ScrollBarSlider, this);
      if (!slider.isValid()) return;
      // Centred in the slot, whatever the stylesheet's box left us.
      const QRectF pill =
          orientation() == Qt::Vertical
              ? QRectF(slider.center().x() + 0.5 - thick_ / 2.0, slider.top(), thick_, slider.height())
              : QRectF(slider.left(), slider.center().y() + 0.5 - thick_ / 2.0, slider.width(), thick_);
      const bool hovered = (opt.activeSubControls & QStyle::SC_ScrollBarSlider) || underMouse();
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing);
      p.setPen(Qt::NoPen);
      p.setBrush(hovered && hover_.isValid() ? hover_ : thumb_);
      p.drawRoundedRect(pill, thick_ / 2.0, thick_ / 2.0);
    }

   private:
    void swellTo(qreal target) {
      if (support::motionReduced()) { swell_->stop(); thick_ = target; update(); return; }
      swell_->stop();
      swell_->setStartValue(thick_);
      swell_->setEndValue(target);
      swell_->start();
    }

    QColor thumb_, hover_;
    qreal thick_ = kRestThick;
    QVariantAnimation* swell_ = nullptr;
  };

  class OverlayScrollArea : public QScrollArea {
   public:
    explicit OverlayScrollArea(QWidget* parent = nullptr) : QScrollArea(parent) {
      setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
      setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
      vBar_ = new PillScrollBar(Qt::Vertical, this);
      hBar_ = new PillScrollBar(Qt::Horizontal, this);
      mirror(verticalScrollBar(), vBar_);
      mirror(horizontalScrollBar(), hBar_);
    }

    // Theme hook (MainWindow::applyTheme): the thumb's rest and hover colours.
    void setThumbColors(const QColor& thumb, const QColor& hover) {
      vBar_->setThumbColors(thumb, hover);
      hBar_->setThumbColors(thumb, hover);
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

    PillScrollBar* vBar_ = nullptr;
    PillScrollBar* hBar_ = nullptr;
  };

}  // namespace stencil::gui

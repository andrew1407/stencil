#pragma once
// Every QScrollBar painted as a rounded pill (browser --sb-thumb / --sb-thumb-hover). The
// stylesheet still SIZES the bars; painting lives here because QSS on macOS draws the
// handle square and its `::handle:hover` never lit the dialogs' bars.
#include "motionPrefs.hpp"   // motionReduced()

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QVariantAnimation>

namespace stencil::gui {

  class ScrollBarPill : public QObject {
   public:
    static constexpr qreal kRestThick = 6;    // thinner than the browser's 8 at rest…
    static constexpr qreal kHoverThick = 9;   // …and 1.5× under the pointer, as there

    // Theme hook (MainWindow::applyTheme). Installs the app-wide adopter on first use.
    static void setColors(const QColor& thumb, const QColor& hover) {
      colors().thumb = thumb;
      colors().hover = hover;
      adopter();
      for (QWidget* w : QApplication::allWidgets())
        if (auto* bar = qobject_cast<QScrollBar*>(w)) {
          adopt(bar);
          bar->update();
        }
    }

    // Idempotent.
    static void adopt(QScrollBar* bar) {
      if (!bar || bar->property(kProperty).toBool()) return;
      bar->setProperty(kProperty, true);
      new ScrollBarPill(bar);
    }

   protected:
    bool eventFilter(QObject* watched, QEvent* e) override {
      if (watched != bar_) return false;
      switch (e->type()) {
        case QEvent::Paint: return paint();
        case QEvent::Enter: swellTo(kHoverThick); break;
        // Qt withholds the Leave during a drag past the edge; settle once the pointer really left.
        case QEvent::Leave: if (!bar_->isSliderDown()) swellTo(kRestThick); break;
        case QEvent::MouseButtonRelease: if (!bar_->underMouse()) swellTo(kRestThick); break;
        default: break;
      }
      return false;
    }

   private:
    static constexpr const char* kProperty = "stencilScrollBarPill";
    struct Colors { QColor thumb, hover; };
    static Colors& colors() { static Colors c; return c; }

    // A QScrollBar's Polish (before its first paint) is the moment it is adopted.
    class Adopter : public QObject {
     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Polish)
          if (auto* bar = qobject_cast<QScrollBar*>(o)) ScrollBarPill::adopt(bar);
        return false;
      }
    };
    static void adopter() {
      static Adopter* a = nullptr;
      if (a || !qApp) return;
      a = new Adopter;
      a->setParent(qApp);
      qApp->installEventFilter(a);
    }

    explicit ScrollBarPill(QScrollBar* bar) : QObject(bar), bar_(bar) {
      swell_ = new QVariantAnimation(this);
      swell_->setDuration(150);
      swell_->setEasingCurve(QEasingCurve::OutCubic);
      connect(swell_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        thick_ = v.toReal();
        bar_->update();
      });
      bar_->installEventFilter(this);
    }

    // true = painted here; false = unthemed, Qt paints.
    bool paint() {
      const Colors& c = colors();
      if (!c.thumb.isValid()) return false;
      // What QScrollBar::initStyleOption (protected) fills in, so the QSS sizing rules apply.
      QStyleOptionSlider opt;
      opt.initFrom(bar_);
      opt.subControls = QStyle::SC_All;
      opt.orientation = bar_->orientation();
      if (opt.orientation == Qt::Horizontal) opt.state |= QStyle::State_Horizontal;
      opt.minimum = bar_->minimum();
      opt.maximum = bar_->maximum();
      opt.sliderPosition = bar_->sliderPosition();
      opt.sliderValue = bar_->value();
      opt.singleStep = bar_->singleStep();
      opt.pageStep = bar_->pageStep();
      opt.upsideDown = bar_->invertedAppearance();
      const QRect slider = bar_->style()->subControlRect(QStyle::CC_ScrollBar, &opt,
                                                         QStyle::SC_ScrollBarSlider, bar_);
      if (!slider.isValid()) return true;   // nothing to scroll: a bare, transparent slot
      const QRectF pill =
          opt.orientation == Qt::Vertical
              ? QRectF(slider.center().x() + 0.5 - thick_ / 2.0, slider.top(), thick_, slider.height())
              : QRectF(slider.left(), slider.center().y() + 0.5 - thick_ / 2.0, slider.width(), thick_);
      const bool hot = bar_->underMouse() || bar_->isSliderDown();
      QPainter p(bar_);
      p.setRenderHint(QPainter::Antialiasing);
      p.setPen(Qt::NoPen);
      p.setBrush(hot && c.hover.isValid() ? c.hover : c.thumb);
      p.drawRoundedRect(pill, thick_ / 2.0, thick_ / 2.0);
      return true;
    }

    void swellTo(qreal target) {
      if (support::motionReduced()) { swell_->stop(); thick_ = target; bar_->update(); return; }
      swell_->stop();
      swell_->setStartValue(thick_);
      swell_->setEndValue(target);
      swell_->start();
    }

    QScrollBar* bar_ = nullptr;
    qreal thick_ = kRestThick;
    QVariantAnimation* swell_ = nullptr;
  };

}  // namespace stencil::gui

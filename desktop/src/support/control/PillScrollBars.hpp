#pragma once
// Every QScrollBar painted as a rounded pill (browser --sb-thumb / --sb-thumb-hover). The
// stylesheet still SIZES the bars; painting lives here because QSS on macOS draws the
// handle square and its `::handle:hover` never lit the dialogs' bars.
#include "motionPrefs.hpp"   // motionReduced()
#include "../skinPrefs.hpp"

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
    static constexpr qreal REST_THICK = 6;    // thinner than the browser's 8 at rest…
    static constexpr qreal HOVER_THICK = 9;   // …and 1.5× under the pointer, as there

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
      if (!bar || bar->property(PROPERTY).toBool()) return;
      bar->setProperty(PROPERTY, true);
      new ScrollBarPill(bar);
    }

   protected:
    bool eventFilter(QObject* watched, QEvent* e) override {
      if (watched != bar) return false;
      switch (e->type()) {
        case QEvent::Paint: return paint();
        case QEvent::Enter: swellTo(HOVER_THICK); break;
        // Qt withholds the Leave during a drag past the edge; settle once the pointer really left.
        case QEvent::Leave: if (!bar->isSliderDown()) swellTo(REST_THICK); break;
        case QEvent::MouseButtonRelease: if (!bar->underMouse()) swellTo(REST_THICK); break;
        default: break;
      }
      return false;
    }

   private:
    static constexpr const char* PROPERTY = "stencilScrollBarPill";
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

    explicit ScrollBarPill(QScrollBar* bar) : QObject(bar), bar(bar) {
      swell = new QVariantAnimation(this);
      swell->setDuration(150);
      swell->setEasingCurve(QEasingCurve::OutCubic);
      connect(swell, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        thick = v.toReal();
        this->bar->update();
      });
      this->bar->installEventFilter(this);
    }

    // true = painted here; false = unthemed, Qt paints.
    bool paint() {
      const Colors& c = colors();
      if (!c.thumb.isValid()) return false;
      // What QScrollBar::initStyleOption (protected) fills in, so the QSS sizing rules apply.
      QStyleOptionSlider opt;
      opt.initFrom(bar);
      opt.subControls = QStyle::SC_All;
      opt.orientation = bar->orientation();
      if (opt.orientation == Qt::Horizontal) opt.state |= QStyle::State_Horizontal;
      opt.minimum = bar->minimum();
      opt.maximum = bar->maximum();
      opt.sliderPosition = bar->sliderPosition();
      opt.sliderValue = bar->value();
      opt.singleStep = bar->singleStep();
      opt.pageStep = bar->pageStep();
      opt.upsideDown = bar->invertedAppearance();
      const QRect slider = bar->style()->subControlRect(QStyle::CC_ScrollBar, &opt,
                                                         QStyle::SC_ScrollBarSlider, bar);
      const bool hot = bar->underMouse() || bar->isSliderDown();
      if (support::isWebcore()) {   // the skin sheet's bevelled bar; the thumb lit in the chosen accent
        QPainter p(bar);
        bar->style()->drawComplexControl(QStyle::CC_ScrollBar, &opt, &p, bar);
        if (hot && slider.isValid() && support::skinAccent().isValid())
          p.fillRect(slider.adjusted(2, 2, -2, -2), support::skinAccent());
        return true;
      }
      if (!slider.isValid()) return true;   // nothing to scroll: a bare, transparent slot
      const QRectF pill =
          opt.orientation == Qt::Vertical
              ? QRectF(slider.center().x() + 0.5 - thick / 2.0, slider.top(), thick, slider.height())
              : QRectF(slider.left(), slider.center().y() + 0.5 - thick / 2.0, slider.width(), thick);
      QPainter p(bar);
      p.setRenderHint(QPainter::Antialiasing);
      p.setPen(Qt::NoPen);
      p.setBrush(hot && c.hover.isValid() ? c.hover : c.thumb);
      p.drawRoundedRect(pill, thick / 2.0, thick / 2.0);
      return true;
    }

    void swellTo(qreal target) {
      if (support::motionReduced()) { swell->stop(); thick = target; bar->update(); return; }
      swell->stop();
      swell->setStartValue(thick);
      swell->setEndValue(target);
      swell->start();
    }

    QScrollBar* bar = nullptr;
    qreal thick = REST_THICK;
    QVariantAnimation* swell = nullptr;
  };

}  // namespace stencil::gui

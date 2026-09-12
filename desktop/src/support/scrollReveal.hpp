#pragma once
// Scroll reveal: the desktop port of browser/js/ui/motion.js .reveal-item. Opacity is a
// pure function of where an item sits in the viewport — no timer, no MOC.
#include <QEvent>
#include <QGraphicsOpacityEffect>

#include "dissolveEffect.hpp"
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QScrollArea>
#include <QScrollBar>
#include <QWidget>
#include <QtGlobal>

#include <algorithm>

namespace stencil::gui {

  // Fade band per viewport edge, px. A real band at the top, a token one at the bottom:
  // the transcript's NEWEST card is pinned flush there and must not read as dimmed.
  inline constexpr int kRevealTopBandPx = 64;
  inline constexpr int kRevealBottomBandPx = 16;
  // Browser .reveal-item: 0.18.
  inline constexpr double kRevealMinOpacity = 0.18;
  // Browser REVEAL_MAX_VISIBLE_DISSOLVE: a row you can still see WHOLE never dissolves
  // away completely (a 29px card inside a 64px band would vanish while on screen).
  inline constexpr double kRevealMaxVisibleDissolve = 0.75;

  // 0 while wholly on screen, rising with the CLIPPED share, 1 once gone. Only the part
  // the viewport already cuts off dissolves — legibility first. Mirrors motion.js. Pure.
  inline double revealDissolve(int top, int bottom, int viewH) {
    const int h = bottom - top;
    if (viewH <= 0 || h <= 0) return 0.0;
    const int visible = std::max(0, std::min(bottom, viewH) - std::max(top, 0));
    return 1.0 - static_cast<double>(visible) / h;
  }

  // NOT revealDissolve: a card TALLER than the viewport is clipped by definition and
  // would sit permanently speckled. Browser motion.js revealGrain. Pure.
  inline double revealGrain(int top, int bottom, int viewH) {
    const int h = bottom - top;
    if (viewH <= 0 || h <= 0) return 0.0;
    const int visible = std::max(0, std::min(bottom, viewH) - std::max(top, 0));
    return 1.0 - static_cast<double>(visible) / std::min(h, viewH);
  }

  // Overlap with the clear box, ramped from kRevealMinOpacity to 1.0. Measuring the
  // OVERLAP keeps a short row flush to the bottom edge readable. Pure.
  inline double revealOpacity(int top, int bottom, int viewH,
                              int topBand = kRevealTopBandPx,
                              int bottomBand = kRevealBottomBandPx) {
    if (viewH <= 0 || bottom <= top) return 1.0;
    if (bottom <= 0 || top >= viewH) return kRevealMinOpacity;   // fully out of view
    const int clearTop = topBand, clearBottom = viewH - bottomBand;
    if (clearBottom <= clearTop) return 1.0;   // viewport too short to have bands at all
    const int overlap = std::max(0, std::min(bottom, clearBottom) - std::max(top, clearTop));
    // Capped by the box's own height: a TALLER item counts as fully revealed once it fills it.
    const int den = std::min(bottom - top, clearBottom - clearTop);
    const double frac = std::min(1.0, static_cast<double>(overlap) / den);
    return kRevealMinOpacity + frac * (1.0 - kRevealMinOpacity);
  }

  // `itemRect` is the delegate's option.rect (viewport coords). Null viewport = no fade.
  inline double revealOpacityForItem(const QWidget* viewport, const QRect& itemRect) {
    return revealOpacity(itemRect.top(), itemRect.bottom() + 1, viewport ? viewport->height() : 0);
  }

  // Null viewport = no dissolve.
  inline double revealDissolveForItem(const QWidget* viewport, const QRect& itemRect) {
    return revealDissolve(itemRect.top(), itemRect.bottom() + 1, viewport ? viewport->height() : 0);
  }

  // Attach once; re-applies on every scroll, resize and layout change. A child opts OUT
  // for its entrance via "stencilEntering" — two writers on one effect would flicker.
  class ScrollReveal : public QObject {
   public:
    static constexpr const char* kEnteringProperty = "stencilEntering";
    // …and permanently via "stencilRevealExempt": floating chrome pinned at the edge.
    static constexpr const char* kExemptProperty = "stencilRevealExempt";

    explicit ScrollReveal(QScrollArea* area) : QObject(area), area_(area) {
      if (!area_) return;
      connect(area_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { apply(); });
      connect(area_->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] { apply(); });
      if (area_->viewport()) area_->viewport()->installEventFilter(this);
      if (area_->widget()) area_->widget()->installEventFilter(this);
      apply();
    }

    void apply() {
      if (!area_ || !area_->widget() || !area_->viewport()) return;
      QWidget* content = area_->widget();
      const int viewH = area_->viewport()->height();
      // A transcript that fits its viewport has no edges to dissolve at.
      const bool scrollable = area_->verticalScrollBar() &&
                              area_->verticalScrollBar()->maximum() > 0;
      for (QObject* o : content->children()) {
        auto* child = qobject_cast<QWidget*>(o);
        if (!child || child->isHidden()) continue;
        if (child->property(kEnteringProperty).toBool()) continue;
        if (child->property(kExemptProperty).toBool()) continue;
        if (!scrollable) { setDissolveOn(child, 0.0, 0.0, 1.0); continue; }
        const int top = child->mapTo(area_->viewport(), QPoint(0, 0)).y();
        const int h = child->height();
        // GRAIN uses the viewport-relative measure so a tall card is not speckled forever;
        // the visible SPAN still comes from the clipped share.
        const double d = revealGrain(top, top + h, viewH);
        const double visStart = h > 0 ? std::clamp(double(0 - top) / h, 0.0, 1.0) : 0.0;
        const double visEnd = h > 0 ? std::clamp(double(viewH - top) / h, 0.0, 1.0) : 1.0;
        setDissolveOn(child, d, visStart, visEnd);
      }
    }

   protected:
    bool eventFilter(QObject* o, QEvent* e) override {
      if (e->type() == QEvent::Resize || e->type() == QEvent::LayoutRequest ||
          e->type() == QEvent::ChildAdded || e->type() == QEvent::Show)
        apply();
      return QObject::eventFilter(o, e);
    }

   private:
    // dynamic_cast, not qobject_cast: DissolveEffect is Q_OBJECT-free. A card mid-entrance
    // still carries ChatDock's QGraphicsOpacityEffect (kEnteringProperty guards the gap).
    static void setDissolveOn(QWidget* w, double dissolve, double visStart, double visEnd) {
      auto* fx = dynamic_cast<DissolveEffect*>(w->graphicsEffect());
      if (!fx) {
        if (dissolve <= 0.0) return;   // whole — don't allocate an effect to say so
        fx = new DissolveEffect(w);
        w->setGraphicsEffect(fx);      // the widget takes ownership (and frees any prior)
      }
      fx->setVisibleSpan(visStart, visEnd);
      fx->setDissolve(dissolve);
    }

   public:
    // The ramp bottoms out at kRevealMinOpacity; rescaled so an out-of-view row reaches a FULL dissolve.
    static double dissolveFor(double revealOpacity) {
      const double span = 1.0 - kRevealMinOpacity;
      if (span <= 0.0) return 0.0;
      return std::clamp((1.0 - revealOpacity) / span, 0.0, 1.0);
    }

   private:

    QScrollArea* area_ = nullptr;
  };

}  // namespace stencil::gui

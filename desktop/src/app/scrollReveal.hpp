#pragma once
// Scroll reveal: the desktop port of browser/js/ui/motion.js + the .reveal-item rules
// in browser/css/animations.css and extension/src/lib/animations.css. Items in a long
// scroller dissolve toward the viewport edges instead of being clipped hard, so
// scrolling reads as motion.
//
// Unlike the web version there is no in/out class toggle with a CSS transition: a
// widget's opacity is a pure function of where it currently sits in the viewport, so
// the ramp is already smooth as the scrollbar moves — and it needs no timer.
//
// Two consumers, one curve (revealOpacity):
//   ScrollReveal          — QScrollArea of laid-out child widgets (the chat transcript)
//   revealOpacityForItem  — item-view delegates, where rows are painted, not widgets
//                           (ProjectRowDelegate in dialogs/projectsDialog.cpp)
//
// Header-only and Q_OBJECT-free (no new signals/slots), so it needs no MOC.
#include <QEvent>
#include <QGraphicsOpacityEffect>

#include "../support/dissolveEffect.hpp"
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QScrollArea>
#include <QScrollBar>
#include <QWidget>
#include <QtGlobal>

#include <algorithm>

namespace stencil::gui {

  // How far the fade band reaches in from each viewport edge, in pixels — i.e. the inset
  // of the "clear box" an item must sit inside to be painted at full opacity. Matches the
  // web's -10%/-2% root margin in spirit: a real band at the top, a token one at the
  // bottom, because a transcript's NEWEST card is pinned flush to that edge and a barely
  // dimmed newest message would read as a bug.
  inline constexpr int kRevealTopBandPx = 64;
  inline constexpr int kRevealBottomBandPx = 16;
  // The rest opacity of a fully out-of-view item (browser .reveal-item: 0.18).
  inline constexpr double kRevealMinOpacity = 0.18;
  // The hard invariant (browser REVEAL_MAX_VISIBLE_DISSOLVE): a row you can still see
  // WHOLE never dissolves away completely. Without it a card shorter than the top band
  // — the transcript's are 29px against a 64px band — fits entirely inside the band and
  // vanishes while fully on screen, i.e. readable text you can scroll to but not read.
  inline constexpr double kRevealMaxVisibleDissolve = 0.75;

  // How dissolved an item spanning [top, bottom) is in a viewport `viewH` tall: 0 while
  // wholly on screen, rising with the CLIPPED share, 1 once it is gone.
  //
  // Deliberately not a "band" inside the visible area: dissolving what you can still
  // read turns a message into an unreadable dot screen (the grain is finer than a
  // glyph's strokes). Decoration must never cost legibility, so only the part the
  // viewport is already cutting off dissolves. Mirrors browser/js/ui/motion.js. Pure.
  inline double revealDissolve(int top, int bottom, int viewH) {
    const int h = bottom - top;
    if (viewH <= 0 || h <= 0) return 0.0;
    const int visible = std::max(0, std::min(bottom, viewH) - std::max(top, 0));
    return 1.0 - static_cast<double>(visible) / h;
  }

  // GRAIN for the same item — and deliberately NOT revealDissolve. The dot screen
  // covers the whole widget, so measuring the clipped share is wrong for anything
  // TALLER than the viewport: such a card is clipped by definition, so it would sit
  // there permanently speckled (a tall variant card showed grain bands across its
  // picture no matter how you scrolled). Showing as much as the viewport can hold
  // counts as fully visible. Browser motion.js revealGrain. Pure.
  inline double revealGrain(int top, int bottom, int viewH) {
    const int h = bottom - top;
    if (viewH <= 0 || h <= 0) return 0.0;
    const int visible = std::max(0, std::min(bottom, viewH) - std::max(top, 0));
    return 1.0 - static_cast<double>(visible) / std::min(h, viewH);
  }

  // Opacity for an item spanning [top, bottom) in viewport coordinates of a viewport
  // `viewH` tall: how much of the item lies inside the clear box, ramped from
  // kRevealMinOpacity to 1.0. Measuring the OVERLAP (not the edge crossing) is what
  // keeps a short row flush to the bottom edge readable while the same row flush to the
  // top — well inside the deeper band — dims away. Pure; unit-tested headless.
  inline double revealOpacity(int top, int bottom, int viewH,
                              int topBand = kRevealTopBandPx,
                              int bottomBand = kRevealBottomBandPx) {
    if (viewH <= 0 || bottom <= top) return 1.0;
    if (bottom <= 0 || top >= viewH) return kRevealMinOpacity;   // fully out of view
    const int clearTop = topBand, clearBottom = viewH - bottomBand;
    if (clearBottom <= clearTop) return 1.0;   // viewport too short to have bands at all
    const int overlap = std::max(0, std::min(bottom, clearBottom) - std::max(top, clearTop));
    // Capped by the clear box's own height, so an item TALLER than the box still counts
    // as fully revealed once it fills it.
    const int den = std::min(bottom - top, clearBottom - clearTop);
    const double frac = std::min(1.0, static_cast<double>(overlap) / den);
    return kRevealMinOpacity + frac * (1.0 - kRevealMinOpacity);
  }

  // The same curve for an item-view row: `itemRect` is the delegate's option.rect, which
  // is already in viewport coordinates. A null viewport (or a zero-height one) means no
  // fade rather than a guess.
  inline double revealOpacityForItem(const QWidget* viewport, const QRect& itemRect) {
    return revealOpacity(itemRect.top(), itemRect.bottom() + 1, viewport ? viewport->height() : 0);
  }

  // The dissolve for an item-view row (the delegate's option.rect is already in
  // viewport coordinates). A null viewport means no dissolve rather than a guess.
  inline double revealDissolveForItem(const QWidget* viewport, const QRect& itemRect) {
    return revealDissolve(itemRect.top(), itemRect.bottom() + 1, viewport ? viewport->height() : 0);
  }

  // Fades the direct children of a QScrollArea's content widget as they scroll through
  // its viewport. Attach once; it re-applies on every scroll, resize and layout change.
  //
  // A child can opt OUT for the duration of its own entrance animation by setting the
  // dynamic property "stencilEntering" to true — ChatDock::animateCardIn owns the card's
  // opacity while it plays, and two writers on one effect would flicker.
  class ScrollReveal : public QObject {
   public:
    static constexpr const char* kEnteringProperty = "stencilEntering";
    // …and permanently, with "stencilRevealExempt": floating CHROME that happens
    // to be parented to the scrolled content (the per-row "…" trigger) is not a
    // transcript row. Fading it at the viewport edge — where it is pinned by
    // design — washed the glyph and its outline out, and the dissolve effect
    // REPLACED the button's own accent glow on the way (user report).
    static constexpr const char* kExemptProperty = "stencilRevealExempt";

    explicit ScrollReveal(QScrollArea* area) : QObject(area), area_(area) {
      if (!area_) return;
      connect(area_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { apply(); });
      connect(area_->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] { apply(); });
      if (area_->viewport()) area_->viewport()->installEventFilter(this);
      if (area_->widget()) area_->widget()->installEventFilter(this);
      apply();
    }

    // Re-run the curve over every child. Cheap: geometry reads plus one setDissolve each.
    void apply() {
      if (!area_ || !area_->widget() || !area_->viewport()) return;
      QWidget* content = area_->widget();
      const int viewH = area_->viewport()->height();
      // Nothing to scroll → nothing to reveal. A transcript that fits its viewport
      // has no edges to dissolve at, and fading its first card would just look broken.
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
        // The GRAIN uses the viewport-relative measure (revealGrain), so a card taller
        // than the transcript is not speckled forever; the visible SPAN below still
        // comes from the clipped share, which is what the soft edge follows.
        const double d = revealGrain(top, top + h, viewH);
        // …and WHERE it is still on screen, so the mask keeps exactly that span solid.
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
    // Drive the widget's grain dissolve (support/dissolveEffect.hpp) — the desktop
    // twin of the browser's .reveal-item mask. `value` is the browser's opacity ramp,
    // so a fully revealed row is 1.0; the effect wants the inverse.
    //
    // dynamic_cast, not qobject_cast: DissolveEffect is deliberately Q_OBJECT-free, so
    // it has no metaobject to cast against. A card mid-entrance still carries
    // ChatDock's QGraphicsOpacityEffect — that one is left alone and simply replaced
    // once the entrance hands over (ScrollReveal::kEnteringProperty guards the gap).
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
    // Map the reveal ramp onto the dissolve. The ramp bottoms out at kRevealMinOpacity
    // rather than 0, so it is rescaled — an out-of-view row must reach a FULL dissolve,
    // not stop at 82% of one.
    static double dissolveFor(double revealOpacity) {
      const double span = 1.0 - kRevealMinOpacity;
      if (span <= 0.0) return 0.0;
      return std::clamp((1.0 - revealOpacity) / span, 0.0, 1.0);
    }

   private:

    QScrollArea* area_ = nullptr;
  };

}  // namespace stencil::gui

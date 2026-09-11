#pragma once
// The canvas ↔ points-panel separator grip — the desktop take on the browser's
// .panel-resizer::before (layout.css): a slim centred bar that turns accent and
// grows under the cursor. The separator itself is QMainWindow chrome (no widget, and QSS
// cannot animate an image swap), so the grip is painted by this mouse-transparent overlay
// over the separator; MainWindow's app-wide eventFilter drives the hot state from the
// hover/drag events the QMainWindow receives, on the same lerp PillSplitterHandle uses.
//
// Header-only and Q_OBJECT-free (no signals/slots of its own), so it needs no MOC.
#include "modalReveal.hpp"   // support::motionReduced()

#include <QColor>
#include <QEasingCurve>
#include <QPainter>
#include <QVariantAnimation>
#include <QWidget>

#include <cmath>

namespace stencil::gui {
  // Both overlays below are the same widget with a different paintEvent: a decorative,
  // mouse-transparent layer over QMainWindow chrome that fades between rest and hover on
  // the app's own 150ms OutCubic (straight to the end state under reduced motion).
  class DockHoverOverlay : public QWidget {
   public:
    static constexpr int kMs = 150;

    explicit DockHoverOverlay(QWidget* parent) : QWidget(parent) {
      setAttribute(Qt::WA_TransparentForMouseEvents, true);   // the separator keeps the drag
      setAttribute(Qt::WA_NoSystemBackground, true);
      anim_ = new QVariantAnimation(this);
      anim_->setDuration(kMs);
      anim_->setEasingCurve(QEasingCurve::OutCubic);
      QObject::connect(anim_, &QVariantAnimation::valueChanged, this,
                       [this](const QVariant& v) { hot_ = v.toDouble(); update(); });
    }

    void setHot(bool on) {
      const double to = on ? 1.0 : 0.0;
      if (qFuzzyCompare(hot_, to)) return;
      anim_->stop();
      if (support::motionReduced()) { hot_ = to; update(); return; }
      anim_->setStartValue(hot_);
      anim_->setEndValue(to);
      anim_->start();
    }

   protected:
    double hot_ = 0.0;      // 0 at rest … 1 fully hovered

   private:
    QVariantAnimation* anim_ = nullptr;
  };


  class DockGripOverlay : public DockHoverOverlay {
   public:
    // The browser's exact numbers (.panel-resizer::before): a 3px bar, 34px at rest
    // in the hairline grey, 52px and accent hovered/dragged, radius 2 — with the
    // 120ms grow/tint ease the composer pill uses (the browser transitions 0.15s).
    static constexpr double kBarW = 3.0;
    static constexpr double kBarH = 34.0;
    static constexpr double kBarHotH = 52.0;

    using DockHoverOverlay::DockHoverOverlay;

    void setColors(const QColor& rest, const QColor& accent) {
      rest_ = rest;
      accent_ = accent;
      update();
    }

   protected:
    void paintEvent(QPaintEvent*) override {
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing, true);
      const auto ch = [this](int a, int b) { return int(std::lround(a + (b - a) * hot_)); };
      const QColor c(ch(rest_.red(), accent_.red()), ch(rest_.green(), accent_.green()),
                     ch(rest_.blue(), accent_.blue()));
      const double h = kBarH + (kBarHotH - kBarH) * hot_;
      p.setPen(Qt::NoPen);
      p.setBrush(c);
      p.drawRoundedRect(QRectF((width() - kBarW) / 2.0, (height() - h) / 2.0, kBarW, h), 2, 2);
    }

   private:
    QColor rest_;
    QColor accent_;
  };

  // The chat dock's resize EDGE (browser .chat-resizer)
  // The browser tints the whole draggable strip on the panel's docked edge — accent at
  // 30%, on hover and for the drag. The desktop's strip is QMainWindow chrome with no
  // widget of its own, so this paints a mouse-transparent band over the separator, driven
  // from MainWindow's eventFilter. It is handed whatever rect the separator occupies.
  class DockEdgeOverlay : public DockHoverOverlay {
   public:
    static constexpr double kAlpha = 0.30;   // browser: color-mix(--accent 30%, transparent)
    // What the band is DRAWN at, however thin the strip it covers: the horizontal
    // separators are a hairline (theme.cpp keeps the fixed top docks from growing a
    // draggable gap), and a 1px tint is not an affordance anyone can see.
    static constexpr int kMinThickness = 6;

    using DockHoverOverlay::DockHoverOverlay;

    void setAccent(const QColor& accent) { accent_ = accent; update(); }

   protected:
    void paintEvent(QPaintEvent*) override {
      if (hot_ <= 0.001 || !accent_.isValid()) return;
      QColor c = accent_;
      c.setAlphaF(kAlpha * hot_);
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing, true);
      p.fillRect(rect(), c);
    }

   private:
    QColor accent_;
  };

}  // namespace stencil::gui

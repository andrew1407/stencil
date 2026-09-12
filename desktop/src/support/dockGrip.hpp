#pragma once
// The canvas ↔ points-panel separator grip — browser .panel-resizer::before (layout.css).
// The separator is QMainWindow chrome with no widget, so this mouse-transparent overlay
// paints it; MainWindow's eventFilter drives the hot state. Q_OBJECT-free, no MOC.
#include "modalReveal.hpp"   // support::motionReduced()

#include <QColor>
#include <QEasingCurve>
#include <QPainter>
#include <QVariantAnimation>
#include <QWidget>

#include <cmath>

namespace stencil::gui {
  // Both overlays are a mouse-transparent layer over QMainWindow chrome on the app's
  // 150ms OutCubic (straight to the end state under reduced motion).
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
    // Browser .panel-resizer::before: 3px bar, 34px at rest, 52px and accent hovered,
    // radius 2; the browser transitions 0.15s.
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

  // The chat dock's resize EDGE (browser .chat-resizer): a mouse-transparent band over
  // the separator, driven from MainWindow's eventFilter.
  class DockEdgeOverlay : public DockHoverOverlay {
   public:
    static constexpr double kAlpha = 0.30;   // browser: color-mix(--accent 30%, transparent)
    // The horizontal separators are a hairline (theme.cpp), and a 1px tint is no affordance.
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

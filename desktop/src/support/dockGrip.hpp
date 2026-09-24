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
    static constexpr int HOVER_MS = 150;

    explicit DockHoverOverlay(QWidget* parent) : QWidget(parent) {
      setAttribute(Qt::WA_TransparentForMouseEvents, true);   // the separator keeps the drag
      setAttribute(Qt::WA_NoSystemBackground, true);
      anim = new QVariantAnimation(this);
      anim->setDuration(HOVER_MS);
      anim->setEasingCurve(QEasingCurve::OutCubic);
      QObject::connect(anim, &QVariantAnimation::valueChanged, this,
                       [this](const QVariant& v) { hot = v.toDouble(); update(); });
    }

    void setHot(bool on) {
      const double to = on ? 1.0 : 0.0;
      if (qFuzzyCompare(hot, to)) return;
      anim->stop();
      if (support::motionReduced()) { hot = to; update(); return; }
      anim->setStartValue(hot);
      anim->setEndValue(to);
      anim->start();
    }

   protected:
    double hot = 0.0;      // 0 at rest … 1 fully hovered

   private:
    QVariantAnimation* anim = nullptr;
  };


  class DockGripOverlay : public DockHoverOverlay {
   public:
    // Browser .panel-resizer::before: 3px bar, 34px at rest, 52px and accent hovered,
    // radius 2; the browser transitions 0.15s.
    static constexpr double BAR_W = 3.0;
    static constexpr double BAR_H = 34.0;
    static constexpr double BAR_HOT_H = 52.0;

    using DockHoverOverlay::DockHoverOverlay;

    void setColors(const QColor& rest, const QColor& accent) {
      this->rest = rest;
      this->accent = accent;
      update();
    }

   protected:
    void paintEvent(QPaintEvent*) override {
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing, true);
      const auto ch = [this](int a, int b) { return int(std::lround(a + (b - a) * hot)); };
      const QColor c(ch(rest.red(), accent.red()), ch(rest.green(), accent.green()),
                     ch(rest.blue(), accent.blue()));
      const double h = BAR_H + (BAR_HOT_H - BAR_H) * hot;
      p.setPen(Qt::NoPen);
      p.setBrush(c);
      p.drawRoundedRect(QRectF((width() - BAR_W) / 2.0, (height() - h) / 2.0, BAR_W, h), 2, 2);
    }

   private:
    QColor rest;
    QColor accent;
  };

  // The chat dock's resize EDGE (browser .chat-resizer): a mouse-transparent band over
  // the separator, driven from MainWindow's eventFilter.
  class DockEdgeOverlay : public DockHoverOverlay {
   public:
    static constexpr double ALPHA = 0.30;   // browser: color-mix(--accent 30%, transparent)
    // The horizontal separators are a hairline (theme.cpp), and a 1px tint is no affordance.
    static constexpr int MIN_THICKNESS = 6;

    using DockHoverOverlay::DockHoverOverlay;

    void setAccent(const QColor& accent) { this->accent = accent; update(); }
    // The page the band lets through at rest (webcore: --wc-desktop, the browser body); invalid = none.
    void setBase(const QColor& base) { this->base = base; update(); }

   protected:
    void paintEvent(QPaintEvent*) override {
      QPainter p(this);
      if (base.isValid()) p.fillRect(rect(), base);
      if (hot <= 0.001 || !accent.isValid()) return;
      QColor c = accent;
      c.setAlphaF(ALPHA * hot);
      p.fillRect(rect(), c);
    }

   private:
    QColor accent;
    QColor base;
  };

}  // namespace stencil::gui

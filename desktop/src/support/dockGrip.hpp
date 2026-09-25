#pragma once
// The canvas ↔ points-panel separator grip — browser .panel-resizer::before (layout.css).
// The separator is QMainWindow chrome with no widget, so this mouse-transparent overlay
// paints it; MainWindow's eventFilter drives the hot state. Q_OBJECT-free, no MOC.
#include "modalReveal.hpp"   // support::motionReduced()

#include <QColor>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QMouseEvent>
#include <functional>
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

    // The grip's colour `hot` of the way from rest to lit.
    QColor lit(const QColor& rest, const QColor& accent) const {
      const auto ch = [this](int a, int b) { return int(std::lround(a + (b - a) * hot)); };
      return QColor(ch(rest.red(), accent.red()), ch(rest.green(), accent.green()),
                    ch(rest.blue(), accent.blue()));
    }

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
      const double h = BAR_H + (BAR_HOT_H - BAR_H) * hot;
      p.setPen(Qt::NoPen);
      p.setBrush(lit(rest, accent));
      p.drawRoundedRect(QRectF((width() - BAR_W) / 2.0, (height() - h) / 2.0, BAR_W, h), 2, 2);
    }

   private:
    QColor rest;
    QColor accent;
  };

  // The chat dock's resize HANDLE (browser .chat-resizer): a strip inside the dock's own edge
  // wearing the panel grip's pill. It takes the pointer itself; the owner resizes on its word.
  class DockEdgeOverlay : public DockHoverOverlay {
   public:
    static constexpr int THICKNESS = 6;   // .chat-resizer: 6px

    explicit DockEdgeOverlay(QWidget* parent) : DockHoverOverlay(parent) {
      setAttribute(Qt::WA_TransparentForMouseEvents, false);
    }

    void setColors(const QColor& rest, const QColor& accent) {
      this->rest = rest;
      this->accent = accent;
      update();
    }
    // Which way the strip runs: the cursor and the pill follow it.
    void setAxis(Qt::Orientation o) { setCursor(o == Qt::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor); }
    // `move` gets the pointer's travel since the press, in global pixels.
    void setDragHandlers(std::function<void()> begin, std::function<void(const QPoint&)> move,
                         std::function<void()> end) {
      onBegin = std::move(begin);
      onMove = std::move(move);
      onEnd = std::move(end);
    }
    bool isDragging() const { return dragging; }

   protected:
    // A widget shown under a pointer it never met gets a synthetic Enter (the offscreen
    // platform's docs capture): only a pointer actually inside the strip lights it.
    void enterEvent(QEnterEvent* e) override { setHot(rect().contains(e->position().toPoint())); }
    void leaveEvent(QEvent*) override { if (!dragging) setHot(false); }
    void mousePressEvent(QMouseEvent* e) override {
      if (e->button() != Qt::LeftButton) return;
      dragging = true;
      grab = e->globalPosition().toPoint();
      setHot(true);
      if (onBegin) onBegin();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
      if (dragging && onMove) onMove(e->globalPosition().toPoint() - grab);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
      if (e->button() != Qt::LeftButton || !dragging) return;
      dragging = false;
      if (onEnd) onEnd();
      if (!underMouse()) setHot(false);
    }
    void paintEvent(QPaintEvent*) override {
      QPainter p(this);
      if (!rest.isValid()) return;
      p.setRenderHint(QPainter::Antialiasing, true);
      const double len = DockGripOverlay::BAR_H + (DockGripOverlay::BAR_HOT_H - DockGripOverlay::BAR_H) * hot;
      const double w = DockGripOverlay::BAR_W;
      const bool flat = width() > height();   // a top/bottom edge lies along x
      p.setPen(Qt::NoPen);
      p.setBrush(lit(rest, accent));
      p.drawRoundedRect(flat ? QRectF((width() - len) / 2.0, (height() - w) / 2.0, len, w)
                             : QRectF((width() - w) / 2.0, (height() - len) / 2.0, w, len), 2, 2);
    }

   private:
    QColor rest;
    QColor accent;
    std::function<void()> onBegin, onEnd;
    std::function<void(const QPoint&)> onMove;
    QPoint grab;
    bool dragging = false;
  };

}  // namespace stencil::gui

#pragma once
// Grain dissolve — port of the .reveal-item mask in browser/css/animations.css: a dot
// grain UNIONed with a bottom→top wipe (intersect would punch holes through a settled
// row), composited DestinationIn. Q_OBJECT-free: reach it with dynamic_cast.
#include <QBrush>
#include <QGraphicsEffect>
#include <QImage>
#include <QColor>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QPoint>
#include <QSize>
#include <QTransform>
#include <QtGlobal>

#include <algorithm>

namespace stencil::gui {

  class DissolveEffect : public QGraphicsEffect {
   public:
    // Browser mask-size 4/7/11px at offsets 0,0 / 2,3 / 5,1: coprime cells interfere,
    // one lattice alone reads as rectangles.
    struct Grain { int cell; double phaseX, phaseY; double decay; };
    static constexpr Grain GRAINS[] = {
        {4, 0.0, 0.0, 0.78}, {7, 2.0, 3.0, 0.95}, {11, 5.0, 1.0, 1.10}};
    // ≥ half-diagonal / cell (0.707): a full-radius dot covers its corners, so 0 is solid.
    static constexpr double FULL_RADIUS_FRAC = 0.72;

    explicit DissolveEffect(QObject* parent = nullptr) : QGraphicsEffect(parent) {}

    double dissolve() const { return dissolve_; }
    // 0..1 shares of the widget's own height.
    void setVisibleSpan(double start, double end) {
      if (qFuzzyCompare(visStart_ + 1.0, start + 1.0) && qFuzzyCompare(visEnd_ + 1.0, end + 1.0)) return;
      visStart_ = start;
      visEnd_ = end;
      update();
    }
    void setDissolve(double v) {
      const double next = std::clamp(v, 0.0, 1.0);
      if (qFuzzyCompare(dissolve_ + 1.0, next + 1.0)) return;
      dissolve_ = next;
      update();
    }

    // For a delegate with no widget to hang an effect on.
    static QImage maskFor(const QSize& size, double dissolve, qreal dpr = 1.0,
                          double visStart = 0.0, double visEnd = 1.0) {
      QImage mask(size * dpr, QImage::Format_ARGB32_Premultiplied);
      mask.setDevicePixelRatio(dpr);
      mask.fill(Qt::transparent);
      QPainter mp(&mask);
      mp.setRenderHint(QPainter::Antialiasing, true);
      const QRectF box(0, 0, size.width(), size.height());
      fillGrain(mp, box, dissolve);
      applyWipe(mp, box, visStart, visEnd);
      return mask;
    }

   protected:
    void draw(QPainter* painter) override {
      if (dissolve_ <= 0.001) { drawSource(painter); return; }

      QPoint offset;
      const QPixmap src = sourcePixmap(Qt::LogicalCoordinates, &offset);
      if (src.isNull()) { drawSource(painter); return; }
      if (dissolve_ >= 0.999) return;

      QImage out = src.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
      QImage mask(out.size(), QImage::Format_ARGB32_Premultiplied);
      mask.setDevicePixelRatio(out.devicePixelRatio());
      mask.fill(Qt::transparent);
      {
        QPainter mp(&mask);
        mp.setRenderHint(QPainter::Antialiasing, true);
        const QRectF box(0, 0, mask.width() / mask.devicePixelRatio(),
                         mask.height() / mask.devicePixelRatio());
        fillGrain(mp, box, dissolve_);
        applyWipe(mp, box, visStart_, visEnd_);
      }
      QPainter op(&out);
      op.setCompositionMode(QPainter::CompositionMode_DestinationIn);
      op.drawImage(0, 0, mask);
      op.end();
      painter->drawImage(offset, out);
    }

   private:
    // One tiled fill per grid, not hundreds of ellipse calls.
    static QBrush grainBrush(const Grain& g, double dissolve) {
      // Each grid thins at its own rate, so they drop out in sequence.
      const double r = g.cell * FULL_RADIUS_FRAC * (1.0 - dissolve * g.decay / 0.78);
      QPixmap tile(g.cell, g.cell);
      tile.fill(Qt::transparent);
      if (r > 0.05) {
        QPainter tp(&tile);
        tp.setRenderHint(QPainter::Antialiasing, true);
        tp.setPen(Qt::NoPen);
        tp.setBrush(Qt::black);
        tp.drawEllipse(QPointF(g.cell / 2.0, g.cell / 2.0), r, r);
      }
      QBrush b(tile);
      b.setTransform(QTransform::fromTranslate(g.phaseX, g.phaseY));
      return b;
    }

    // The still-visible span, kept solid (browser --vis-start/--vis-end wipe). QGradient
    // clamps stops to 0..1, unlike CSS — a stop at 1.0 overwrote the transparent one.
    static void applyWipe(QPainter& p, const QRectF& box, double visStart, double visEnd) {
      if (visEnd <= visStart) return;
      QLinearGradient wipe(box.topLeft(), box.bottomLeft());
      const double feather = 0.10;
      const double a = std::min(1.0, visStart + feather);
      const double b = std::max(0.0, visEnd - feather);
      wipe.setColorAt(0.0, Qt::transparent);
      if (visStart > 0.0) wipe.setColorAt(visStart, Qt::transparent);
      wipe.setColorAt(std::min(a, b), Qt::black);
      wipe.setColorAt(std::max(a, b), Qt::black);
      if (visEnd < 1.0) wipe.setColorAt(visEnd, Qt::transparent);
      wipe.setColorAt(1.0, Qt::transparent);
      p.fillRect(box, wipe);
    }

    static void fillGrain(QPainter& p, const QRectF& box, double dissolve) {
      for (const Grain& g : GRAINS) p.fillRect(box, grainBrush(g, dissolve));
    }

    double dissolve_ = 0.0;
    double visStart_ = 0.0, visEnd_ = 1.0;
  };

}  // namespace stencil::gui

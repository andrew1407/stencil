#pragma once
// Grain dissolve — the desktop port of the .reveal-item mask in
// browser/css/animations.css.
//
// The browser masks a row with two layers UNIONed: a tiled dot grain whose dots shrink,
// plus a bottom→top wipe marking the region still fully intact. Qt has no CSS masks, so
// the same two layers are painted into an alpha mask here and composited over the
// widget with DestinationIn. Union, not intersect — intersect would punch dot-holes
// through a settled row.
//
// `dissolve` is 0 (whole) to 1 (gone). At 0 the effect short-circuits to a plain
// drawSource, so a settled row costs nothing beyond the effect's own indirection.
//
// Header-only and Q_OBJECT-free (no signals/slots), so it needs no MOC. Callers reach
// it with dynamic_cast, since qobject_cast needs the metaobject a Q_OBJECT would add.
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
    // THREE grain grids at different cell sizes and phases (browser: mask-size
    // 4/7/11px at offsets 0,0 / 2,3 / 5,1). One grid alone is a regular lattice — its
    // dots line up and merge into joining rectangles instead of reading as sand.
    // Coprime cells at different phases interfere, so the specks land irregularly.
    struct Grain { int cell; double phaseX, phaseY; double decay; };
    static constexpr Grain kGrains[] = {
        {4, 0.0, 0.0, 0.78}, {7, 2.0, 3.0, 0.95}, {11, 5.0, 1.0, 1.10}};
    // A circle of this share of a cell covers its corners (half-diagonal / cell ≈ 0.707),
    // so at dissolve 0 every grain layer is fully opaque and the row is solid.
    static constexpr double kFullRadiusFrac = 0.72;

    explicit DissolveEffect(QObject* parent = nullptr) : QGraphicsEffect(parent) {}

    double dissolve() const { return dissolve_; }
    // The still-visible span of the widget, as a 0..1 share of its own height.
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

    // The mask on its own, for callers that have no widget to hang an effect on —
    // an item-view delegate paints its rows, so it renders to a scratch pixmap and
    // composites this itself. Same two layers, same union.
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
      if (dissolve_ <= 0.001) { drawSource(painter); return; }   // whole — nothing to mask

      QPoint offset;
      const QPixmap src = sourcePixmap(Qt::LogicalCoordinates, &offset);
      if (src.isNull()) { drawSource(painter); return; }
      if (dissolve_ >= 0.999) return;                            // gone — draw nothing

      QImage out = src.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
      QImage mask(out.size(), QImage::Format_ARGB32_Premultiplied);
      mask.setDevicePixelRatio(out.devicePixelRatio());
      mask.fill(Qt::transparent);
      {
        QPainter mp(&mask);
        mp.setRenderHint(QPainter::Antialiasing, true);
        const QRectF box(0, 0, mask.width() / mask.devicePixelRatio(),
                         mask.height() / mask.devicePixelRatio());
        // 1) the grain, as ONE tiled fill rather than a few hundred ellipse calls.
        fillGrain(mp, box, dissolve_);
        // 2) the still-intact region, unioned on top (both painted = opaque where either).
        applyWipe(mp, box, visStart_, visEnd_);
      }
      QPainter op(&out);
      op.setCompositionMode(QPainter::CompositionMode_DestinationIn);
      op.drawImage(0, 0, mask);
      op.end();
      painter->drawImage(offset, out);
    }

   private:
    // A cell-sized tile holding one black dot, used as a repeating brush. The dot
    // shrinks to nothing as the dissolve completes — that is what turns the edge into
    // speckle instead of a clean line.
    static QBrush grainBrush(const Grain& g, double dissolve) {
      // Each grid thins at its own rate, so they drop out in sequence rather than all
      // vanishing at once — which is what leaves a trailing haze of fine specks.
      const double r = g.cell * kFullRadiusFrac * (1.0 - dissolve * g.decay / 0.78);
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

    // The bottom→top wipe: the region still fully intact, its edge climbing as the
    // dissolve rises (browser: linear-gradient(to top, transparent d*150%, #000
    // d*150% + 45%)). CSS lets gradient stops sit outside the box; QGradient clamps
    // every position to 0..1, so the out-of-range cases have to be handled by hand —
    // clamping alone let a stop at 1.0 overwrite the transparent one and left a
    // near-solid card at high dissolve.
    static void applyWipe(QPainter& p, const QRectF& box, double visStart, double visEnd) {
      // The STILL-VISIBLE span of the widget, kept solid. The grain only ever shows
      // outside it, so the part you can read is never sanded — only the edge the
      // viewport is already cutting off. (Browser: the --vis-start/--vis-end wipe.)
      if (visEnd <= visStart) return;            // nothing of it is on screen
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

    // Paint all three grids, unioned, into the current painter.
    static void fillGrain(QPainter& p, const QRectF& box, double dissolve) {
      for (const Grain& g : kGrains) p.fillRect(box, grainBrush(g, dissolve));
    }

    double dissolve_ = 0.0;
    double visStart_ = 0.0, visEnd_ = 1.0;
  };

}  // namespace stencil::gui

#include "indicators.hpp"

#include "artCache.hpp"
#include "gradient.hpp"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace stencil::support {

  namespace {
    constexpr double CELL = 14;
    constexpr QRectF CELL_RECT(0, 2, CELL, CELL);

    bool isRadio(AquaIndicator k) {
      return k == AquaIndicator::RADIO || k == AquaIndicator::RADIO_ON || k == AquaIndicator::RADIO_DEAD_ON;
    }
    bool isOn(AquaIndicator k) { return k != AquaIndicator::BOX && k != AquaIndicator::RADIO; }
    // A dead control is glass, never gel (browser chrome.css :disabled), its mark kept.
    bool isGel(AquaIndicator k) { return k == AquaIndicator::BOX_ON || k == AquaIndicator::RADIO_ON; }

    QPainterPath cellPath(bool radio) {
      QPainterPath path;
      if (radio) path.addEllipse(CELL_RECT);
      else path.addRoundedRect(CELL_RECT, 2, 2);
      return path;
    }

    // ::after { left 5px; top -3px; 5×11 with a 3px right and bottom border; rotate(40deg) skewX(-6deg) }
    // about its own centre, (9, 4) from the cell's corner.
    QPainterPath tickPath() {
      QPainterPath tall, foot;   // united, never two addRects: the even-odd overlap cut the elbow out
      tall.addRect(QRectF(1, -7, 3, 14));
      foot.addRect(QRectF(-4, 4, 8, 3));
      const QPainterPath l = tall.united(foot);
      QTransform t;
      t.translate(CELL_RECT.left() + 9, CELL_RECT.top() + 4);
      t.rotate(40);
      t.shear(std::tan(-6 * M_PI / 180), 0);
      return t.map(l);
    }

    void paint(QPainter& p, AquaIndicator kind, const AquaIndicatorColors& c) {
      const bool isRound = isRadio(kind), isGelled = isGel(kind);
      const QPainterPath cell = cellPath(isRound);
      p.setPen(Qt::NoPen);
      // 0 1px 2px rgba(0,0,0,.22 | .3): one offset copy stands in for the blur.
      p.setBrush(QColor(0, 0, 0, isGelled ? 46 : 34));
      p.drawPath(cell.translated(0, 1));
      QLinearGradient fill(CELL_RECT.topLeft(), CELL_RECT.bottomLeft());
      fill.setStops(isGelled ? aquaGelStops(c.gelHi, c.gelTop, c.gelBottom) : aquaGlassStops(c.glassTop, c.glassBottom));
      p.setBrush(fill);
      p.drawPath(cell);
      // inset 0 0 0 1px: the ring inside the cell.
      p.setPen(QPen(isGelled ? c.gelEdge : c.edge, 1));
      p.setBrush(Qt::NoBrush);
      const QRectF ring = CELL_RECT.adjusted(0.5, 0.5, -0.5, -0.5);
      if (isRound) p.drawEllipse(ring);
      else p.drawRoundedRect(ring, 1.5, 1.5);
      if (!isOn(kind)) return;
      p.setPen(Qt::NoPen);
      p.setBrush(c.tick);
      if (isRound) p.drawEllipse(CELL_RECT.adjusted(5, 5, -5, -5));   // inset: 5px
      else p.drawPath(tickPath());
    }

    QString key(AquaIndicator kind, const AquaIndicatorColors& c) {
      QString k = QStringLiteral("v2-") + QString::number(static_cast<int>(kind));   // bump when the drawing changes
      for (const QColor& col : {c.glassTop, c.glassBottom, c.edge, c.gelHi, c.gelTop, c.gelBottom, c.gelEdge, c.tick})
        k += '-' + col.name().mid(1);
      return k;
    }
  }  // namespace

  QString aquaIndicatorPath(AquaIndicator kind, const AquaIndicatorColors& c) {
    return cachedArtPath("aq-ind-" + key(kind, c), QSize(AQUA_INDICATOR_PX, AQUA_INDICATOR_PX),
                         [&](QPainter& p) { paint(p, kind, c); });
  }

}  // namespace stencil::support

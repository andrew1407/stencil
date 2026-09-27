#include "CanvasWidget.hpp"
#include "canvasPaintCache.hpp"
#include "markMetrics.hpp"
#include "pointerTuning.hpp"
#include "strokeDash.hpp"
#include "theme.hpp"

#include <QPaintEvent>
#include <QPen>

// paintEvent: the scene drawn at the zoom with the live marks, the rubber bands and the hold ghost
// over it; and where the compare divider sits under the pointer.

namespace stencil::gui {

  void CanvasWidget::paintEvent(QPaintEvent* event) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const Palette& pal = paintPalette(dark, accentKey, selGlow, hoverRing);
    if (image.isNull()) {
      idle.paint(p, pal, dark, accentKey);
      return;
    }
    if (!toolTip().isEmpty()) setToolTip(QString());
    // "original" shows the cropped+rotated original alone (a BLANK page keeps fill + tint); the
    // split modes render the edit and overlay the original on one half at the end.
    if (filterDirty) rebuildFilteredImage();
    const CompareMode compare = effectiveCompareMode();
    const QRectF target(0, 0, image.width() * scale, image.height() * scale);
    if (compare == CompareMode::ORIGINAL) {
      p.drawImage(target, basePremul.of(compareBaseImage()));
      return;
    }
    // A "none" filter leaves no filtered picture behind (rebuildFilteredImage).
    p.drawImage(target, shownPremul.of(filteredImage.isNull() ? image : filteredImage));
    // A split compare view is read-only: no selection glow, no hover/focus rings. A line whose
    // padded bounds miss the exposed rect would paint nothing there, so it is not stroked.
    const bool hl = compare == CompareMode::NONE;
    const QRect exposed = event->rect();
    const LiveMarks marks = liveMarks();
    for (int i = 0; i < static_cast<int>(lines.size()); ++i)
      if (exposed.intersects(lineRect(i))) drawLineScaled(p, lines[i], i, scale, /*highlight=*/hl, &marks);
    if (exposed.intersects(lineRect(-1))) drawLineScaled(p, currentLine, -1, scale, /*highlight=*/hl, &marks);

    if (gesture.is(Gesture::ZOOM_RECT)) {
      QPen pen(pal.accent);
      pen.setStyle(Qt::DashLine);
      pen.setWidth(1);
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawRect(QRectF(gesture.zoomStart, gesture.zoomEnd).normalized());
    }

    // Rect-draw rubber band (browser drawingApp.js), same dashed-accent style as the zoom band.
    if (gesture.is(Gesture::RECT_DRAW)) {
      QPen pen(pal.accent);
      pen.setStyle(Qt::DashLine);
      pen.setWidth(1);
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawRect(QRectF(gesture.rectStart, gesture.rectEnd).normalized());
    }

    // Hold-to-draw ghost segment + point (renderer.js drawHoldPreview). Transient.
    if (hold.hasPreview) {
      const QColor base(QString::fromStdString(
          currentLine.points.empty() && selectedLine()
              ? selectedLine()->color
              : defColor.toStdString()));
      p.save();   // image space under the zoom, like every other stroke
      p.scale(scale, scale);
      const QPointF cur(hold.preview.x, hold.preview.y);
      const pointerTuning::Table& tune = pointerTuning::table();
      if (const core::Point* a = holdAnchor()) {
        QColor line = base; line.setAlphaF(tune.holdGhostLineAlpha);
        const double width = std::max(1.0, defThickness);
        QPen pen(line);
        pen.setWidthF(width);
        pen.setDashPattern(strokeDash::inPenUnits(tune.holdGhostDashPx, width));
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(a->x, a->y), cur);
      }
      QColor dot = base; dot.setAlphaF(tune.holdGhostPointAlpha);
      p.setPen(Qt::NoPen);
      p.setBrush(dot);
      p.drawEllipse(cur, defPointSize, defPointSize);
      p.restore();
    }

    if (compare == CompareMode::VERTICAL || compare == CompareMode::HORIZONTAL)
      paintCompareSplit(p, compare, scale);
  }

  bool CanvasWidget::nearCompareDivider(const QPoint& widgetPos) const {
    if (!isSplitCompare()) return false;
    const double f = std::clamp(compareSplit, 0.0, 1.0);
    const double tol = markMetrics::table().dividerGrabSlackPx;
    if (compareMode == CompareMode::VERTICAL)
      return std::abs(widgetPos.x() - image.width() * scale * f) <= tol;
    return std::abs(widgetPos.y() - image.height() * scale * f) <= tol;
  }

}  // namespace stencil::gui

#include "CanvasWidget.hpp"
#include "canvasPaintCache.hpp"
#include "CanvasWidget.hpp"
#include "theme.hpp"

#include <QPen>

// paintEvent and the compare-mode split it draws.

namespace stencil::gui {

  void CanvasWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const Palette& pal = paintPalette(dark, accentKey, selGlow, hoverRing);
    if (image.isNull()) {
      paintIdleCard(p, pal);
      return;
    }
    if (!toolTip().isEmpty()) setToolTip(QString());
    // "original" shows the cropped+rotated original alone (a BLANK page keeps fill + tint); the
    // split modes render the edit and overlay the original on one half at the end.
    if (filterDirty) rebuildFilteredImage();
    const QString compare = effectiveCompareMode();
    if (compare == "original") {
      p.drawImage(QRectF(0, 0, image.width() * scale, image.height() * scale),
                  compareBaseImage());
      return;
    }

    const QImage& shown = (imageFilter == "none" || filteredImage.isNull())
                              ? image
                              : filteredImage;
    p.drawImage(QRectF(0, 0, image.width() * scale, image.height() * scale),
                shown);
    // A split compare view is read-only: no selection glow, no hover/focus rings.
    const bool hl = compare == "none";
    for (int i = 0; i < static_cast<int>(lines.size()); ++i)
      drawLineScaled(p, lines[i], i, scale, /*highlight=*/hl);
    drawLineScaled(p, currentLine, -1, scale, /*highlight=*/hl);

    if (zoomRectActive) {
      QPen pen(pal.accent);
      pen.setStyle(Qt::DashLine);
      pen.setWidth(1);
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawRect(QRectF(zoomRectStart, zoomRectEnd).normalized());
    }

    // Rect-draw rubber band (browser drawingApp.js), same dashed-accent style as the zoom band.
    if (rectDrawActive) {
      QPen pen(pal.accent);
      pen.setStyle(Qt::DashLine);
      pen.setWidth(1);
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawRect(QRectF(rectDrawStart, rectDrawEnd).normalized());
    }

    // Hold-to-draw ghost segment + point (renderer.js drawHoldPreview). Transient.
    if (holdHasPreview) {
      const QColor base(QString::fromStdString(
          currentLine.points.empty() && selectedLine()
              ? selectedLine()->color
              : defColor.toStdString()));
      p.save();   // image space under the zoom, like every other stroke
      p.scale(scale, scale);
      const QPointF cur(holdPreview.x, holdPreview.y);
      if (const core::Point* a = holdAnchor()) {
        QColor line = base; line.setAlphaF(0.45);
        QPen pen(line);
        pen.setStyle(Qt::DashLine);
        pen.setWidthF(std::max(1.0, defThickness));
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(a->x, a->y), cur);
      }
      QColor dot = base; dot.setAlphaF(0.6);
      p.setPen(Qt::NoPen);
      p.setBrush(dot);
      p.drawEllipse(cur, defPointSize, defPointSize);
      p.restore();
    }

    if (compare == "vertical" || compare == "horizontal") paintCompareSplit(p, compare, scale);
  }

  // Divider metrics are in widget space: constant on-screen thickness at any zoom.
  void CanvasWidget::paintCompareSplit(QPainter& p, const QString& mode, double scale, bool withDivider) const {
    const double w = image.width() * scale;
    const double h = image.height() * scale;
    const double f = std::clamp(compareSplit, 0.0, 1.0);

    p.save();
    p.setClipRect(mode == "vertical" ? QRectF(0, 0, w * f, h) : QRectF(0, 0, w, h * f));
    // A blank page keeps its fill + tint (its colour IS the page); a picture drops the filter too.
    p.drawImage(QRectF(0, 0, w, h), compareBaseImage());
    p.restore();

    if (!withDivider) return;

    p.save();
    p.setClipping(false);
    // Two passes so the white divider reads on ANY background (the browser's drop shadow).
    QPen casing(QColor(0, 0, 0, 150), 4);
    casing.setCapStyle(Qt::RoundCap);
    QPen white(QColor(255, 255, 255, 240), 2);
    white.setCapStyle(Qt::RoundCap);
    const double r = 7.0;
    if (mode == "vertical") {
      const double x = w * f;
      p.setPen(casing);
      p.drawLine(QPointF(x, 0), QPointF(x, h));
      p.setPen(white);
      p.drawLine(QPointF(x, 0), QPointF(x, h));
      p.setPen(QPen(QColor(0, 0, 0, 150), 1.5));
      p.setBrush(QColor(255, 255, 255, 240));
      p.drawEllipse(QPointF(x, h / 2), r, r);
    } else {
      const double y = h * f;
      p.setPen(casing);
      p.drawLine(QPointF(0, y), QPointF(w, y));
      p.setPen(white);
      p.drawLine(QPointF(0, y), QPointF(w, y));
      p.setPen(QPen(QColor(0, 0, 0, 150), 1.5));
      p.setBrush(QColor(255, 255, 255, 240));
      p.drawEllipse(QPointF(w / 2, y), r, r);
    }
    p.restore();
  }

  // The original covers x < w*f / y < h*f (the clip rects paintCompareSplit uses).
  bool CanvasWidget::compareShowsEdited(double imageX, double imageY) const {
    const QString mode = effectiveCompareMode();
    if (mode == QLatin1String("none")) return true;
    const double f = std::clamp(compareSplit, 0.0, 1.0);
    if (mode == QLatin1String("vertical")) return imageX >= image.width() * f;
    if (mode == QLatin1String("horizontal")) return imageY >= image.height() * f;
    return false;  // "original": the edit (and its layout) is nowhere on screen
  }

  bool CanvasWidget::nearCompareDivider(const QPoint& widgetPos) const {
    if (compareMode != "vertical" && compareMode != "horizontal") return false;
    const double f = std::clamp(compareSplit, 0.0, 1.0);
    const double tol = 8.0;
    if (compareMode == "vertical")
      return std::abs(widgetPos.x() - image.width() * scale * f) <= tol;
    return std::abs(widgetPos.y() - image.height() * scale * f) <= tol;
  }

}  // namespace stencil::gui

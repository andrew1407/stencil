#include "canvasWidget.hpp"
#include "canvasPaintCache.hpp"
#include "canvasWidget.hpp"
#include "theme.hpp"

#include <QPen>

// paintEvent and the compare-mode split it draws.

namespace stencil::gui {

  void CanvasWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const Palette& pal = paintPalette(dark_, accentKey_, selGlow_, hoverRing_);
    if (image_.isNull()) {
      paintIdleCard(p, pal);
      return;
    }
    // An image is loaded: the idle card's tooltip must not linger over the artwork.
    if (!toolTip().isEmpty()) setToolTip(QString());
    // Compare: "original" shows the cropped+rotated original alone (a BLANK page
    // keeps its fill + tint, compareBaseImage); the split modes render the edit
    // normally and overlay the original on one half at the end of this function.
    if (filterDirty_) rebuildFilteredImage();
    const QString compare = effectiveCompareMode();
    if (compare == "original") {
      p.drawImage(QRectF(0, 0, image_.width() * scale_, image_.height() * scale_),
                  compareBaseImage());
      return;
    }

    // Draw the raw image when no filter, else the cached filtered copy
    // (rebuilt lazily above).
    const QImage& shown = (imageFilter_ == "none" || filteredImage_.isNull())
                              ? image_
                              : filteredImage_;
    p.drawImage(QRectF(0, 0, image_.width() * scale_, image_.height() * scale_),
                shown);
    // A split compare view is read-only: draw lines/points but with no selection glow or
    // hover/focus rings (a clean picture to compare against).
    const bool hl = compare == "none";
    for (int i = 0; i < static_cast<int>(lines_.size()); ++i)
      drawLineScaled(p, lines_[i], i, scale_, /*highlight=*/hl);
    drawLineScaled(p, currentLine_, -1, scale_, /*highlight=*/hl);

    // Zoom-to-rect rubber band preview.
    if (zoomRectActive_) {
      QPen pen(pal.accent);
      pen.setStyle(Qt::DashLine);
      pen.setWidth(1);
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawRect(QRectF(zoomRectStart_, zoomRectEnd_).normalized());
    }

    // drag-to-create rectangle rubber band (browser drawingApp.js rect-draw
    // overlay). Same dashed-accent style as the zoom band.
    if (rectDrawActive_) {
      QPen pen(pal.accent);
      pen.setStyle(Qt::DashLine);
      pen.setWidth(1);
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawRect(QRectF(rectDrawStart_, rectDrawEnd_).normalized());
    }

    // Hold-to-draw: faded dashed segment from the stroke's anchor to the held
    // cursor + a ghost point — mirrors renderer.js drawHoldPreview. Transient.
    if (holdHasPreview_) {
      const QColor base(QString::fromStdString(
          currentLine_.points.empty() && selectedLine()
              ? selectedLine()->color
              : defColor_.toStdString()));
      const QPointF cur(holdPreview_.x * scale_, holdPreview_.y * scale_);
      if (const core::Point* a = holdAnchor()) {
        QColor line = base; line.setAlphaF(0.45);
        QPen pen(line);
        pen.setStyle(Qt::DashLine);
        pen.setWidthF(std::max(1.0, defThickness_));
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(a->x * scale_, a->y * scale_), cur);
      }
      QColor dot = base; dot.setAlphaF(0.6);
      p.setPen(Qt::NoPen);
      p.setBrush(dot);
      p.drawEllipse(cur, defPointSize_, defPointSize_);
    }

    // Split compare: overlay the untouched original on the original-side half (covering
    // the filtered pixels + annotations there) and draw the movable divider on top.
    if (compare == "vertical" || compare == "horizontal") paintCompareSplit(p, compare, scale_);
  }

  // Paint the original over the "original" half of a split compare view (left for
  // "vertical", top for "horizontal") and draw the draggable divider. Divider metrics are
  // in widget space, so the line keeps a constant on-screen thickness at any zoom.
  void CanvasWidget::paintCompareSplit(QPainter& p, const QString& mode, double scale, bool withDivider) const {
    const double w = image_.width() * scale;
    const double h = image_.height() * scale;
    const double f = std::clamp(compareSplit_, 0.0, 1.0);

    p.save();
    p.setClipRect(mode == "vertical" ? QRectF(0, 0, w * f, h) : QRectF(0, 0, w, h * f));
    // Original — no annotations; a blank page keeps its fill + tint (its colour
    // IS the page), a picture drops the filter too.
    p.drawImage(QRectF(0, 0, w, h), compareBaseImage());
    p.restore();

    if (!withDivider) return;

    p.save();
    p.setClipping(false);
    // Two passes so the white divider + handle read on ANY background (a white line
    // vanishes on the white original side) — a dark casing under the white line and a
    // dark ring around the white knob, mirroring the browser's drop shadow.
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

  // Which half of a compare view a point lands in (image space). Mirrors the clip
  // rects paintCompareSplit uses: the original covers x < w*f / y < h*f, the edit
  // holds the rest.
  bool CanvasWidget::compareShowsEdited(double imageX, double imageY) const {
    const QString mode = effectiveCompareMode();
    if (mode == QLatin1String("none")) return true;
    const double f = std::clamp(compareSplit_, 0.0, 1.0);
    if (mode == QLatin1String("vertical")) return imageX >= image_.width() * f;
    if (mode == QLatin1String("horizontal")) return imageY >= image_.height() * f;
    return false;  // "original": the edit (and its layout) is nowhere on screen
  }

  // Whether widgetPos is within grab distance (widget px) of the split divider.
  bool CanvasWidget::nearCompareDivider(const QPoint& widgetPos) const {
    if (compareMode_ != "vertical" && compareMode_ != "horizontal") return false;
    const double f = std::clamp(compareSplit_, 0.0, 1.0);
    const double tol = 8.0;
    if (compareMode_ == "vertical")
      return std::abs(widgetPos.x() - image_.width() * scale_ * f) <= tol;
    return std::abs(widgetPos.y() - image_.height() * scale_ * f) <= tol;
  }

}  // namespace stencil::gui

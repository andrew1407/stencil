#include "CanvasWidget.hpp"
#include "canvasPaintCache.hpp"
#include "CanvasWidget.hpp"
#include "theme.hpp"

// Rect drawing, image-space mapping and the scaled line/point geometry.

namespace stencil::gui {

  // Port of browser drawingApp.js createRect (standalone branch): a locked 4-corner rectangle
  // (drawPolygon closes the loop, so no 5th point).
  void CanvasWidget::createRect(double x1, double y1, double x2, double y2) {
    const double xa = std::min(x1, x2);
    const double xb = std::max(x1, x2);
    const double ya = std::min(y1, y2);
    const double yb = std::max(y1, y2);

    // Continuation drawing: append the 4 corners to the line being extended
    // instead of making a standalone area (drawingApp.js createRect ~1402).
    if (continueLineIdx_ >= 0 &&
        continueLineIdx_ < static_cast<int>(lines_.size())) {
      core::Line& line = lines_[continueLineIdx_];
      const int at = std::max(
          0, std::min(continueInsertIdx_, static_cast<int>(line.points.size())));
      const core::Point corners[4] = {
          {xa, ya}, {xb, ya}, {xb, yb}, {xa, yb}};
      line.points.insert(line.points.begin() + at, corners, corners + 4);
      flyInPoints(continueLineIdx_, line, at, 4);
      continueInsertIdx_ = at + 4;
      selectedPoint_ = continueInsertIdx_ - 1;
      commitHistory();
      update();
      emit selectionChanged();
      return;
    }

    core::Line rect;
    rect.points = {{xa, ya}, {xb, ya}, {xb, yb}, {xa, yb}};  // exactly 4 corners
    rect.color = defColor_.toStdString();
    rect.pointColor =
        (defPointColor_.isEmpty() ? defColor_ : defPointColor_).toStdString();
    rect.thickness = defThickness_;
    rect.pointSize = defPointSize_;
    rect.style = defStyle_.toStdString();
    rect.locked = true;
    rect.fillColor = "transparent";

    lines_.push_back(rect);
    selectedLineIdx_ = static_cast<int>(lines_.size()) - 1;
    // The rect draws itself out of its first corner, edge by edge.
    flyInPoints(selectedLineIdx_, lines_.back(), 0, 4);
    selectedPoint_ = -1;
    commitHistory();
    update();
    emit selectionChanged();
  }

  core::Point CanvasWidget::toImageSpace(int widgetX, int widgetY) const {
    return {widgetX / scale_, widgetY / scale_};
  }

  // Port of the line drawing in browser/js/core/renderer.js. Scale-parameterized so renderToImage
  // draws at native resolution while the live view passes scale_.
  void CanvasWidget::drawLineScaled(QPainter& p, const core::Line& line,
                                    int lineIdx, double scale,
                                    bool highlight, bool live) const {
    if (line.points.empty()) return;

    // A vertex added a moment ago is drawn where it is RIGHT NOW, so segments hanging off a moving
    // vertex follow it for free. One buffer per frame: QPolygonF keeps its capacity.
    static thread_local QPolygonF polyBuf;
    flownPolygon(line, lineIdx, scale, live, polyBuf);
    const QPolygonF& poly = polyBuf;

    const QColor stroke = paintColor(line.color);
    const Palette& pal = paintPalette(dark_, accentKey_, selGlow_, hoverRing_);

    // core::pointColorOr resolves an unset point colour to the stroke colour, so a line without one
    // paints as it always did; an unparseable colour falls back there too, never to black.
    const QColor pointParsed = paintColor(core::pointColorOr(line));
    const QColor pointFill = pointParsed.isValid() ? pointParsed : stroke;

    drawFill(p, line, poly);
    drawGlow(p, line, poly, lineIdx, highlight, pal);
    if (live) drawStrokeWake(p, line, poly, lineIdx, stroke);
    drawStroke(p, line, poly, stroke);
    drawPoints(p, line, poly, lineIdx, highlight, pointFill, pal, live);
    if (live) drawStrokeSpark(p, line, poly, lineIdx, pointFill);
  }

  // The flight, painted (browser js/core/strokeFx.js)
  // How much bigger than its resting size a vertex is drawn right now.
  double CanvasWidget::pointScaleAt(int lineIdx, const core::Line& line, int ptIdx,
                                    bool live) const {
    if (!live || ptIdx < 0 || ptIdx >= static_cast<int>(line.points.size())) return 1.0;
    const stroke::Flight* f = strokeFx_.at(lineIdx, line.points[ptIdx]);
    if (!f) return 1.0;
    return stroke::vertexScale(stroke::phase(fxNow() - f->start, f->fly));
  }

  // ms on the shared clock. Every pass takes it here so they all agree on one instant.
  double CanvasWidget::fxNow() const {
    return static_cast<double>(fxClock_.elapsed());
  }

}  // namespace stencil::gui

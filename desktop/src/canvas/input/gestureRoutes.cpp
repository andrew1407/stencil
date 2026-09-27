#include "gestureRoutes.hpp"
#include "CanvasWidget.hpp"
#include "canvasPaintCache.hpp"

#include <QMouseEvent>
#include <algorithm>
#include <array>
#include <cmath>

// The per-gesture handlers the table routes to. Every move drops a tooltip left over from before
// its gesture began.

namespace stencil::gui {

  namespace {
    constexpr std::size_t GESTURE_KINDS = static_cast<std::size_t>(Gesture::COMPARE_SPLIT) + 1;

    void translate(std::vector<core::Point>& pts, const std::vector<core::Point>& orig, double dx, double dy) {
      for (std::size_t i = 0; i < pts.size() && i < orig.size(); ++i) {
        pts[i].x = orig[i].x + dx;
        pts[i].y = orig[i].y + dy;
      }
    }
  }  // namespace

  const GestureRoute& GestureRoutes::of(Gesture g) {
    static const std::array<GestureRoute, GESTURE_KINDS> routes = [] {
      std::array<GestureRoute, GESTURE_KINDS> r{};
      const auto at = [&r](Gesture k) -> GestureRoute& { return r[static_cast<std::size_t>(k)]; };
      at(Gesture::COMPARE_SPLIT) = {moveCompareSplit, endCompareSplit};
      at(Gesture::POINT) = {drag<dragPoint>, endDrag};
      at(Gesture::SEGMENT) = {drag<dragSegment>, endDrag};
      at(Gesture::LINE) = {drag<dragLine>, endDrag};
      at(Gesture::PAN) = {movePan, endPan};
      at(Gesture::ZOOM_RECT) = {moveZoomRect, endZoomRect};
      at(Gesture::RECT_DRAW) = {moveRectDraw, endRectDraw};
      return r;
    }();
    return routes[static_cast<std::size_t>(g)];
  }

  void GestureRoutes::moveCompareSplit(CanvasWidget& c, QMouseEvent* e) {
    const double f = c.compareMode == CanvasScene::CompareMode::VERTICAL
                         ? e->pos().x() / (c.image.width() * c.scale)
                         : e->pos().y() / (c.image.height() * c.scale);
    c.setCompareSplit(f);   // clamps + repaints
    emit c.hoverLeft();
  }

  // Shift is read per event and applied to the snapshot, so toggling it never accumulates.
  template <GestureRoutes::DragStep step>
  void GestureRoutes::drag(CanvasWidget& c, QMouseEvent* e) {
    const core::Point ip = c.toImageSpace(e->pos().x(), e->pos().y());
    const bool shift = bool(e->modifiers() & Qt::ShiftModifier);
    c.gesture.moved = true;
    // Repaint only the lines that move, not the whole zoomed page.
    const QRect before = c.dragRect();
    step(c, ip, shift);
    c.update(before.united(c.dragRect()));
    emit c.hoverLeft();
  }

  void GestureRoutes::dragPoint(CanvasWidget& c, const core::Point& at, bool) {
    const CanvasGesture& g = c.gesture;
    core::Line* line = (g.lineIdx < 0) ? &c.currentLine : &c.lines[g.lineIdx];
    if (g.ptIdx1 >= 0 && g.ptIdx1 < static_cast<int>(line->points.size()))
      line->points[g.ptIdx1] = at;   // snap point to cursor
  }

  // Shift moves the whole line; without it only the grabbed segment's two endpoints move.
  void GestureRoutes::dragSegment(CanvasWidget& c, const core::Point& at, bool shift) {
    const CanvasGesture& g = c.gesture;
    const double dx = at.x - g.start.x;
    const double dy = at.y - g.start.y;
    core::Line& line = c.lines[g.lineIdx];
    if (shift) {
      translate(line.points, g.orig, dx, dy);
      return;
    }
    line.points = g.orig;   // reset, then move only the two endpoints
    for (int pi : {g.ptIdx1, g.ptIdx2}) {
      line.points[pi].x = g.orig[pi].x + dx;
      line.points[pi].y = g.orig[pi].y + dy;
    }
  }

  // Always the whole line, even when Shift lifts a beat before the mouse — degrading to the
  // grabbed segment would snap the rest back on commit.
  void GestureRoutes::dragLine(CanvasWidget& c, const core::Point& at, bool) {
    const CanvasGesture& g = c.gesture;
    const double dx = at.x - g.start.x;
    const double dy = at.y - g.start.y;
    if (g.multiOrig.empty()) {
      translate(c.lines[g.lineIdx].points, g.orig, dx, dy);
      return;
    }
    for (const auto& [li, orig] : g.multiOrig)
      if (li >= 0 && li < static_cast<int>(c.lines.size())) translate(c.lines[li].points, orig, dx, dy);
  }

  void GestureRoutes::movePan(CanvasWidget& c, QMouseEvent* e) {
    // Delta in GLOBAL coords: the scroll we trigger moves this widget under the pointer.
    const QPoint gp = e->globalPosition().toPoint();
    const QPoint d = gp - c.gesture.lastPanPos;
    c.gesture.lastPanPos = gp;
    emit c.panBy(d.x(), d.y(), bool(e->modifiers() & Qt::ShiftModifier));
    emit c.hoverLeft();
  }

  void GestureRoutes::moveZoomRect(CanvasWidget& c, QMouseEvent* e) {
    CanvasGesture& g = c.gesture;
    const QRect before = bandRect(g.zoomStart, g.zoomEnd);
    g.zoomEnd = e->pos();
    c.update(before.united(bandRect(g.zoomStart, g.zoomEnd)));
    emit c.hoverLeft();
  }

  void GestureRoutes::moveRectDraw(CanvasWidget& c, QMouseEvent* e) {
    CanvasGesture& g = c.gesture;
    const QRect before = bandRect(g.rectStart, g.rectEnd);
    g.rectEnd = e->pos();
    c.update(before.united(bandRect(g.rectStart, g.rectEnd)));
    emit c.hoverLeft();
  }

  void GestureRoutes::endCompareSplit(CanvasWidget& c, QMouseEvent*) {
    c.gesture.end(Gesture::COMPARE_SPLIT);
    c.unsetCursor();
  }

  // Commit one undo step only when a committed line actually moved; an in-progress-line point
  // edit just refreshes the panel (drawingApp.js mouseup).
  void GestureRoutes::endDrag(CanvasWidget& c, QMouseEvent*) {
    CanvasGesture& g = c.gesture;
    const bool moved = g.moved;
    const bool committed = moved && g.lineIdx >= 0;
    const QRect dirty = c.dragRect();   // before the kind clears it
    g.kind = Gesture::NONE;
    g.lineIdx = g.ptIdx1 = g.ptIdx2 = -1;
    g.orig.clear();
    g.multiOrig.clear();
    c.unsetCursor();
    c.update(dirty);
    if (committed) c.commitHistory();   // emits changed()
    if (moved) emit c.selectionChanged();
  }

  void GestureRoutes::endPan(CanvasWidget& c, QMouseEvent*) {
    c.gesture.end(Gesture::PAN);
    c.unsetCursor();
  }

  // The swept band in image space frames a zoom only past 4x4 image px (drawingApp.js mouseup).
  void GestureRoutes::endZoomRect(CanvasWidget& c, QMouseEvent*) {
    CanvasGesture& g = c.gesture;
    g.end(Gesture::ZOOM_RECT);
    const QRect band = bandRect(g.zoomStart, g.zoomEnd);
    const core::Point a = c.toImageSpace(g.zoomStart.x(), g.zoomStart.y());
    const core::Point b = c.toImageSpace(g.zoomEnd.x(), g.zoomEnd.y());
    const double x1 = std::min(a.x, b.x);
    const double y1 = std::min(a.y, b.y);
    const double w = std::abs(b.x - a.x);
    const double h = std::abs(b.y - a.y);
    c.update(band);
    if (w > 4.0 && h > 4.0) emit c.zoomToRect(QRectF(x1, y1, w, h));
  }

  // A drag-to-create rectangle lands only past 3 image px on both axes (drawingApp.js mouseup).
  void GestureRoutes::endRectDraw(CanvasWidget& c, QMouseEvent*) {
    CanvasGesture& g = c.gesture;
    g.end(Gesture::RECT_DRAW);
    const core::Point a = c.toImageSpace(g.rectStart.x(), g.rectStart.y());
    const core::Point b = c.toImageSpace(g.rectEnd.x(), g.rectEnd.y());
    if (std::abs(b.x - a.x) > 3.0 && std::abs(b.y - a.y) > 3.0) c.createRect(a.x, a.y, b.x, b.y);
    c.update();
  }

}  // namespace stencil::gui

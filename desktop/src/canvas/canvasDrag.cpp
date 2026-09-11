#include "canvasWidget.hpp"
#include "canvasWidget.hpp"
#include "hitTest.hpp"

// The drags a press can begin: Alt-drag, pull-out, zoom rect, Ctrl-click.

namespace stencil::gui {

  // Alt+left (port of startPan ~721/~764): drag the point, segment, or (with
  // Shift) whole line under the cursor, else pan. Takes precedence over drawing —
  // no points are added while editing/panning. Every path sets state + returns.
  void CanvasWidget::beginAltDrag(const core::Point& ip,
                                  Qt::KeyboardModifiers mods,
                                  const QPoint& globalPos) {
    dragStart_ = ip;
    dragMoved_ = false;

    // Alt+Ctrl -> pull a new point out of whatever is under the cursor and drag it; on a
    // closed area the same pull breaks the shape open there. Checked before the plain Alt
    // drags, which would otherwise move the point already there.
    if ((mods & Qt::ControlModifier) && !(mods & Qt::ShiftModifier) && beginPullOut(ip)) return;

    // Alt+Shift over a line -> whole-line drag (always translates EVERY point).
    if (mods & Qt::ShiftModifier) {
      const int li = core::findLineAt(lines_, ip.x, ip.y, hitRadius(8.0));
      if (li != -1) {
        dragKind_ = DragKind::Line;
        dragLineIdx_ = li;
        dragOrig_ = lines_[li].points;
        // If the grabbed line is part of a multi-selection, snapshot EVERY selected line so the
        // drag translates them all together (whole-line move).
        dragMultiOrig_.clear();
        const auto sel = selectedIndices();
        if (sel.size() >= 2 && std::find(sel.begin(), sel.end(), li) != sel.end())
          for (int i : sel) dragMultiOrig_.push_back({i, lines_[i].points});
        dragPtIdx1_ = dragPtIdx2_ = -1;
        setCursor(Qt::SizeAllCursor);
        return;
      }
    }

    // Priority 1: near a point (in-progress line first) -> drag the point.
    if (auto idx = core::nearestPointInLine(currentLine_.points, ip.x, ip.y,
                                            hitRadius(12.0))) {
      dragKind_ = DragKind::Point;
      dragLineIdx_ = -1;  // in-progress line
      dragPtIdx1_ = *idx;
      setCursor(Qt::SizeAllCursor);
      return;
    }
    if (auto pt = core::findNearestPoint(lines_, ip.x, ip.y, hitRadius(12.0))) {
      dragKind_ = DragKind::Point;
      dragLineIdx_ = pt->lineIdx;
      dragPtIdx1_ = pt->ptIdx;
      setCursor(Qt::SizeAllCursor);
      return;
    }
    // Priority 2: near a segment -> drag that segment.
    if (auto seg = core::findNearestSegment(lines_, ip.x, ip.y, hitRadius(12.0))) {
      dragKind_ = DragKind::Segment;
      dragLineIdx_ = seg->lineIdx;
      dragPtIdx1_ = seg->ptIdx1;
      dragPtIdx2_ = seg->ptIdx2;
      dragOrig_ = lines_[seg->lineIdx].points;
      setCursor(Qt::SizeAllCursor);
      return;
    }
    // Otherwise: pan (drawingApp.js startPan ~797).
    panning_ = true;
    lastPanPos_ = globalPos;  // global: see middle-button note
    setCursor(Qt::ClosedHandCursor);
  }

  // Pull a new point out of the line under `ip` and start dragging it. A locked area is
  // opened at that spot first, so the seam appears where the user grabbed rather than
  // always at point 0 (chainEdit.hpp). False when there is nothing to pull out of.
  bool CanvasWidget::beginPullOut(const core::Point& ip) {
    chain::PullTarget target;
    int lineIdx = -1;
    if (auto pt = core::findNearestPoint(lines_, ip.x, ip.y, hitRadius(12.0))) {
      lineIdx = pt->lineIdx;
      target = {true, pt->ptIdx};
    } else if (auto seg = core::findNearestSegment(lines_, ip.x, ip.y, hitRadius(12.0))) {
      lineIdx = seg->lineIdx;
      target = {false, seg->ptIdx2};
    } else {
      return false;
    }
    if (lineIdx < 0 || lineIdx >= static_cast<int>(lines_.size())) return false;
    core::Line& line = lines_[lineIdx];
    const bool wasArea = line.locked;
    const int idx = chain::pullOutPoint(line, target, ip.x, ip.y);
    if (idx < 0) return false;
    flyInPoint(lineIdx, line, idx);
    selectedLineIdx_ = lineIdx;
    selectedPoint_ = idx;
    dragKind_ = DragKind::Point;
    dragLineIdx_ = lineIdx;
    dragPtIdx1_ = idx;
    setCursor(Qt::SizeAllCursor);
    update();
    emit changed();
    emit selectionChanged();
    if (wasArea) emit statusMessage(tr("Area unchained — drag the loose end"));
    return true;
  }

  // Turn the selected area back into an open line (the selection bar's Unchain button).
  void CanvasWidget::unchainSelectedLine() {
    if (compareReadOnly()) return;
    if (selectedLineIdx_ < 0 || selectedLineIdx_ >= static_cast<int>(lines_.size())) return;
    if (!chain::unchainLine(lines_[selectedLineIdx_])) return;
    resetStrokeFx();   // its points were rebuilt; nothing in the air still belongs to them
    selectedPoint_ = -1;
    commitHistory();
    update();
    emit selectionChanged();
    emit statusMessage(tr("Area unchained — it is an open line again"));
  }

  // Zoom-to-rect: Shift+left-drag sweeps a rubber band (drawingApp.js
  // startPan shift branch ~746). No points added while sweeping. widgetPos is
  // widget space (NOT image space).
  void CanvasWidget::beginZoomRect(const QPoint& widgetPos) {
    zoomRectActive_ = true;
    zoomRectStart_ = zoomRectEnd_ = widgetPos;
    update();
  }

  // Ctrl+left (drawingApp.js canvasClick ~1187/1239): insert a point onto the
  // nearest segment, else (when not drawing) add a point connected to the selection.
  // Returns false only for drawing+Ctrl+no-segment, which falls through to append.
  bool CanvasWidget::handleCtrlClick(const core::Point& ip) {
    if (auto seg = core::findNearestSegment(lines_, ip.x, ip.y, hitRadius(12.0))) {
      insertPointOnSegment(seg->lineIdx, seg->ptIdx2, ip.x, ip.y);
      // Inserting shifts later indices right by one; keep the continuation
      // tail anchored to the same spot (drawingApp.js ~1193).
      if (seg->lineIdx == continueLineIdx_ &&
          seg->ptIdx2 <= continueInsertIdx_) {
        ++continueInsertIdx_;
      }
      return true;
    }
    if (!isDrawing_) {
      addConnectedPoint(ip.x, ip.y);
      return true;
    }
    // drawing + Ctrl + no segment -> normal append handled by the caller.
    return false;
  }

  // Plain left-click drawing: rect-draw press, select-when-not-drawing, rect-mode
  // no-op, continuation extend/close, the close-shape gate, and the normal point
  // append. widgetPos seeds the rect-draw rubber band (widget space).
  // core::shouldCloseShape grabs within `pointSize + 8` image pixels, where every other
  // hit test here is a screen radius over the zoom (hitRadius) — at 25% the first point
  // was a three-pixel target. core adds its own +8, so hand it the size that makes the
  // total screen-constant. Zoomed out only: magnifying must not shrink the targets.
  double CanvasWidget::closeGrabSize(const core::Line& line) const {
    const double ps = line.pointSize;
    const double scale = scale_ > 0 ? scale_ : 1.0;
    if (scale >= 1.0) return ps;
    return std::max(ps, (ps + kCloseSlack) / scale - kCloseSlack);
  }

}  // namespace stencil::gui

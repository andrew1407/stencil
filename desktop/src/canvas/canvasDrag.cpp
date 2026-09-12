#include "canvasWidget.hpp"
#include "canvasWidget.hpp"
#include "hitTest.hpp"

// The drags a press can begin: Alt-drag, pull-out, zoom rect, Ctrl-click.

namespace stencil::gui {

  // Alt+left (browser startPan): point, segment or (Shift) whole line under the cursor, else pan.
  void CanvasWidget::beginAltDrag(const core::Point& ip,
                                  Qt::KeyboardModifiers mods,
                                  const QPoint& globalPos) {
    dragStart_ = ip;
    dragMoved_ = false;

    // Alt+Ctrl pull-out is checked before the plain Alt drags, which would move the point already there.
    if ((mods & Qt::ControlModifier) && !(mods & Qt::ShiftModifier) && beginPullOut(ip)) return;

    if (mods & Qt::ShiftModifier) {
      const int li = core::findLineAt(lines_, ip.x, ip.y, hitRadius(8.0));
      if (li != -1) {
        dragKind_ = DragKind::Line;
        dragLineIdx_ = li;
        dragOrig_ = lines_[li].points;
        // A multi-selection drags as one: snapshot EVERY selected line.
        dragMultiOrig_.clear();
        const auto sel = selectedIndices();
        if (sel.size() >= 2 && std::find(sel.begin(), sel.end(), li) != sel.end())
          for (int i : sel) dragMultiOrig_.push_back({i, lines_[i].points});
        dragPtIdx1_ = dragPtIdx2_ = -1;
        setCursor(Qt::SizeAllCursor);
        return;
      }
    }

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
    if (auto seg = core::findNearestSegment(lines_, ip.x, ip.y, hitRadius(12.0))) {
      dragKind_ = DragKind::Segment;
      dragLineIdx_ = seg->lineIdx;
      dragPtIdx1_ = seg->ptIdx1;
      dragPtIdx2_ = seg->ptIdx2;
      dragOrig_ = lines_[seg->lineIdx].points;
      setCursor(Qt::SizeAllCursor);
      return;
    }
    panning_ = true;
    lastPanPos_ = globalPos;  // global: see middle-button note
    setCursor(Qt::ClosedHandCursor);
  }

  // A locked area is opened at the grab spot first (chainEdit.hpp). False when nothing is under `ip`.
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

  // Shift+left-drag rubber band (browser startPan shift branch); widgetPos is widget space.
  void CanvasWidget::beginZoomRect(const QPoint& widgetPos) {
    zoomRectActive_ = true;
    zoomRectStart_ = zoomRectEnd_ = widgetPos;
    update();
  }

  // Ctrl+left (browser canvasClick): insert on the nearest segment, else add a connected point.
  // False only for drawing + Ctrl + no segment, which falls through to append.
  bool CanvasWidget::handleCtrlClick(const core::Point& ip) {
    if (auto seg = core::findNearestSegment(lines_, ip.x, ip.y, hitRadius(12.0))) {
      insertPointOnSegment(seg->lineIdx, seg->ptIdx2, ip.x, ip.y);
      // Inserting shifts later indices right; keep the continuation tail anchored.
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
    return false;
  }

  // core::shouldCloseShape grabs within `pointSize + 8` IMAGE px, every other hit test is a screen
  // radius over the zoom — at 25% the first point was a three-pixel target. Zoomed out only.
  double CanvasWidget::closeGrabSize(const core::Line& line) const {
    const double ps = line.pointSize;
    const double scale = scale_ > 0 ? scale_ : 1.0;
    if (scale >= 1.0) return ps;
    return std::max(ps, (ps + kCloseSlack) / scale - kCloseSlack);
  }

}  // namespace stencil::gui

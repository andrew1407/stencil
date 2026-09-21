#include "CanvasWidget.hpp"
#include "CanvasWidget.hpp"
#include "hitTest.hpp"

// The drags a press can begin: Alt-drag, pull-out, zoom rect, Ctrl-click.

namespace stencil::gui {

  // Alt+left (browser startPan): point, segment or (Shift) whole line under the cursor, else pan.
  void CanvasWidget::beginAltDrag(const core::Point& ip,
                                  Qt::KeyboardModifiers mods,
                                  const QPoint& globalPos) {
    dragStart = ip;
    dragMoved = false;

    // Alt+Ctrl pull-out is checked before the plain Alt drags, which would move the point already there.
    if ((mods & Qt::ControlModifier) && !(mods & Qt::ShiftModifier) && beginPullOut(ip)) return;

    if (mods & Qt::ShiftModifier) {
      const int li = core::findLineAt(lines, ip.x, ip.y, hitRadius(8.0));
      if (li != -1) {
        dragKind = DragKind::LINE;
        dragLineIdx = li;
        dragOrig = lines[li].points;
        // A multi-selection drags as one: snapshot EVERY selected line.
        dragMultiOrig.clear();
        const auto sel = selectedIndices();
        if (sel.size() >= 2 && std::find(sel.begin(), sel.end(), li) != sel.end())
          for (int i : sel) dragMultiOrig.push_back({i, lines[i].points});
        dragPtIdx1 = dragPtIdx2 = -1;
        setCursor(Qt::SizeAllCursor);
        return;
      }
    }

    if (auto idx = core::nearestPointInLine(currentLine.points, ip.x, ip.y,
                                            hitRadius(12.0))) {
      dragKind = DragKind::POINT;
      dragLineIdx = -1;  // in-progress line
      dragPtIdx1 = *idx;
      setCursor(Qt::SizeAllCursor);
      return;
    }
    if (auto pt = core::findNearestPoint(lines, ip.x, ip.y, hitRadius(12.0))) {
      dragKind = DragKind::POINT;
      dragLineIdx = pt->lineIdx;
      dragPtIdx1 = pt->ptIdx;
      setCursor(Qt::SizeAllCursor);
      return;
    }
    if (auto seg = core::findNearestSegment(lines, ip.x, ip.y, hitRadius(12.0))) {
      dragKind = DragKind::SEGMENT;
      dragLineIdx = seg->lineIdx;
      dragPtIdx1 = seg->ptIdx1;
      dragPtIdx2 = seg->ptIdx2;
      dragOrig = lines[seg->lineIdx].points;
      setCursor(Qt::SizeAllCursor);
      return;
    }
    panning = true;
    lastPanPos = globalPos;  // global: see middle-button note
    setCursor(Qt::ClosedHandCursor);
  }

  // A locked area is opened at the grab spot first (chainEdit.hpp). False when nothing is under `ip`.
  bool CanvasWidget::beginPullOut(const core::Point& ip) {
    chain::PullTarget target;
    int lineIdx = -1;
    if (auto pt = core::findNearestPoint(lines, ip.x, ip.y, hitRadius(12.0))) {
      lineIdx = pt->lineIdx;
      target = {true, pt->ptIdx};
    } else if (auto seg = core::findNearestSegment(lines, ip.x, ip.y, hitRadius(12.0))) {
      lineIdx = seg->lineIdx;
      target = {false, seg->ptIdx2};
    } else {
      return false;
    }
    if (lineIdx < 0 || lineIdx >= static_cast<int>(lines.size())) return false;
    core::Line& line = lines[lineIdx];
    const bool wasArea = line.locked;
    const int idx = chain::pullOutPoint(line, target, ip.x, ip.y);
    if (idx < 0) return false;
    flyInPoint(lineIdx, line, idx);
    selectedLineIdx = lineIdx;
    selectedPoint = idx;
    dragKind = DragKind::POINT;
    dragLineIdx = lineIdx;
    dragPtIdx1 = idx;
    setCursor(Qt::SizeAllCursor);
    update();
    emit changed();
    emit selectionChanged();
    if (wasArea) emit statusMessage(tr("Area unchained — drag the loose end"));
    return true;
  }

  void CanvasWidget::unchainSelectedLine() {
    if (compareReadOnly()) return;
    if (selectedLineIdx < 0 || selectedLineIdx >= static_cast<int>(lines.size())) return;
    if (!chain::unchainLine(lines[selectedLineIdx])) return;
    resetStrokeFx();   // its points were rebuilt; nothing in the air still belongs to them
    selectedPoint = -1;
    commitHistory();
    update();
    emit selectionChanged();
    emit statusMessage(tr("Area unchained — it is an open line again"));
  }

  // Shift+left-drag rubber band (browser startPan shift branch); widgetPos is widget space.
  void CanvasWidget::beginZoomRect(const QPoint& widgetPos) {
    zoomRectActive = true;
    zoomRectStart = zoomRectEnd = widgetPos;
    update();
  }

  // Ctrl+left (browser canvasClick): insert on the nearest segment, else add a connected point.
  // False only for drawing + Ctrl + no segment, which falls through to append.
  bool CanvasWidget::handleCtrlClick(const core::Point& ip) {
    if (auto seg = core::findNearestSegment(lines, ip.x, ip.y, hitRadius(12.0))) {
      insertPointOnSegment(seg->lineIdx, seg->ptIdx2, ip.x, ip.y);
      // Inserting shifts later indices right; keep the continuation tail anchored.
      if (seg->lineIdx == continueLineIdx &&
          seg->ptIdx2 <= continueInsertIdx) {
        ++continueInsertIdx;
      }
      return true;
    }
    if (!isDrawing) {
      addConnectedPoint(ip.x, ip.y);
      return true;
    }
    return false;
  }

  // core::shouldCloseShape grabs within `pointSize + 8` IMAGE px, every other hit test is a screen
  // radius over the zoom — at 25% the first point was a three-pixel target. Zoomed out only.
  double CanvasWidget::closeGrabSize(const core::Line& line) const {
    const double ps = line.pointSize;
    const double scale = this->scale > 0 ? this->scale : 1.0;
    if (scale >= 1.0) return ps;
    return std::max(ps, (ps + CLOSE_SLACK) / scale - CLOSE_SLACK);
  }

}  // namespace stencil::gui

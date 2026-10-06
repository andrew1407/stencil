#pragma once
#include "canvasCore.hpp"

#include <QPoint>
#include <utility>
#include <vector>

// The pointer gesture in flight: the drags, the pan, the two rubber bands and the compare divider
// as one state, so two can never both be set (browser core/pointer/gesture.js GESTURES).
namespace stencil::gui {

  enum class Gesture { NONE, PAN, POINT, SEGMENT, LINE, ZOOM_RECT, RECT_DRAW, COMPARE_SPLIT };

  struct CanvasGesture {
    Gesture kind = Gesture::NONE;

    bool is(Gesture g) const { return kind == g; }
    // An Alt-drag of a point, a segment or a whole line.
    bool dragging() const { return is(Gesture::POINT) || is(Gesture::SEGMENT) || is(Gesture::LINE); }
    // Raising a gesture ends any other; ending one leaves another alone.
    void end(Gesture g) {
      if (is(g)) kind = Gesture::NONE;
    }

    int lineIdx = -1;   // line being edited (-1 = in-progress line, POINT only)
    int ptIdx1 = -1;    // dragged point (POINT) / grabbed segment endpoint 1
    int ptIdx2 = -1;    // grabbed segment endpoint 2 (SEGMENT / LINE fallback)
    core::Point start;  // image-space cursor at gesture start
    std::vector<core::Point> orig;  // snapshot of the line's points at start
    std::vector<std::pair<int, std::vector<core::Point>>> multiOrig;
    bool moved = false;             // any motion happened (gate history)
    QPoint lastPanPos;              // last cursor pos during a pan (GLOBAL space)
    QPoint zoomStart, zoomEnd;      // the zoom rubber band (widget space)
    QPoint rectStart, rectEnd;      // the rect-draw rubber band (widget space)
    // The point the last drawing click dropped (line -1 = in-progress; dropIdx -1 = none).
    int dropLine = -1, dropIdx = -1;
  };

}  // namespace stencil::gui

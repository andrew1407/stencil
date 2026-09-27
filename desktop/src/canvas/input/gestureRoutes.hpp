#pragma once
#include "gesture.hpp"

class QMouseEvent;

// Each gesture's move and release, looked up by the one state (browser core/pointer/controller.js
// onMove, release.js ON_RELEASE); NONE has neither. The canvas's friend: a handler reaches the
// gesture and the lines it moves.
namespace stencil::gui {

  class CanvasWidget;

  struct GestureRoute {
    void (*move)(CanvasWidget&, QMouseEvent*) = nullptr;
    void (*release)(CanvasWidget&, QMouseEvent*) = nullptr;
  };

  class GestureRoutes {
   public:
    static const GestureRoute& of(Gesture g);

   private:
    using DragStep = void (*)(CanvasWidget& c, const core::Point& at, bool shift);
    template <DragStep step>
    static void drag(CanvasWidget& c, QMouseEvent* e);
    static void dragPoint(CanvasWidget& c, const core::Point& at, bool shift);
    static void dragSegment(CanvasWidget& c, const core::Point& at, bool shift);
    static void dragLine(CanvasWidget& c, const core::Point& at, bool shift);
    static void moveCompareSplit(CanvasWidget& c, QMouseEvent* e);
    static void movePan(CanvasWidget& c, QMouseEvent* e);
    static void moveZoomRect(CanvasWidget& c, QMouseEvent* e);
    static void moveRectDraw(CanvasWidget& c, QMouseEvent* e);
    static void endCompareSplit(CanvasWidget& c, QMouseEvent* e);
    static void endDrag(CanvasWidget& c, QMouseEvent* e);
    static void endPan(CanvasWidget& c, QMouseEvent* e);
    static void endZoomRect(CanvasWidget& c, QMouseEvent* e);
    static void endRectDraw(CanvasWidget& c, QMouseEvent* e);
  };

}  // namespace stencil::gui

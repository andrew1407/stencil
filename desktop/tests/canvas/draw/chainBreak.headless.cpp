// Breaking the chain while drawing (canvas/draw/CanvasChainBreak.cpp, the port of browser
// core/draw/chainBreak.js) on the real widget: ⌘/Ctrl+click and a double-click keep the stroke so
// far — a lone point too — and open an unconnected one at the press, drawing stays on. Offscreen.
#include "CanvasWidget.hpp"

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <cstdio>

#include "../../support/check.hpp"

using stencil::gui::CanvasWidget;

static void send(CanvasWidget& c, QEvent::Type type, QPoint at, Qt::KeyboardModifiers mods = Qt::NoModifier) {
  const Qt::MouseButtons held = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
  QMouseEvent e(type, QPointF(at), c.mapToGlobal(at), Qt::LeftButton, held, mods);
  QCoreApplication::sendEvent(&c, &e);
}
static void click(CanvasWidget& c, QPoint at, Qt::KeyboardModifiers mods = Qt::NoModifier) {
  send(c, QEvent::MouseButtonPress, at, mods);
  send(c, QEvent::MouseButtonRelease, at, mods);
}
// Qt's double-click: press, release, then a DblClick in place of the second press.
static void doubleClick(CanvasWidget& c, QPoint at) {
  click(c, at);
  send(c, QEvent::MouseButtonDblClick, at);
  send(c, QEvent::MouseButtonRelease, at);
}
static bool at(const stencil::core::Point& p, double x, double y) { return p.x == x && p.y == y; }

int main(int argc, char** argv) {
  QApplication app(argc, argv);  // offscreen via QT_QPA_PLATFORM
  CanvasWidget canvas;
  QImage img(400, 300, QImage::Format_RGB32);
  img.fill(Qt::gray);
  canvas.loadFromImage(img);
  canvas.resize(canvas.imageWidth(), canvas.imageHeight());
  canvas.setScale(1.0);

  // ⌘ (Qt's ControlModifier on macOS) / Ctrl + click away from every segment.
  canvas.startDrawingMode();
  click(canvas, {10, 10});
  click(canvas, {200, 10});
  click(canvas, {300, 250}, Qt::ControlModifier);
  check(canvas.getIsDrawing(), "⌘/Ctrl+click keeps drawing mode on");
  check(canvas.getLines().size() == 1 && canvas.getLines()[0].points.size() == 2,
        "the stroke so far is committed as its own line");
  check(canvas.getCurrentLine().points.size() == 1 && at(canvas.getCurrentLine().points[0], 300, 250),
        "the new chain starts at the ⌘/Ctrl+click");
  click(canvas, {350, 250});
  check(canvas.getCurrentLine().points.size() == 2, "the next click extends the new chain");
  canvas.stopDrawingMode();
  canvas.clearAll();

  // A lone point survives the break.
  canvas.startDrawingMode();
  click(canvas, {50, 50});
  click(canvas, {300, 50}, Qt::ControlModifier);
  check(canvas.getLines().size() == 1 && canvas.getLines()[0].points.size() == 1,
        "a one-point chain is kept as a standalone point");
  canvas.stopDrawingMode();
  canvas.clearAll();

  // A double-click moves the point its first press dropped onto a new chain.
  canvas.startDrawingMode();
  click(canvas, {10, 10});
  click(canvas, {200, 10});
  doubleClick(canvas, {200, 200});
  check(canvas.getIsDrawing(), "a double-click keeps drawing mode on");
  check(canvas.getLines().size() == 1 && canvas.getLines()[0].points.size() == 2,
        "the double-click's point is taken back off the old chain");
  check(canvas.getCurrentLine().points.size() == 1 && at(canvas.getCurrentLine().points[0], 200, 200),
        "and opens the new chain, with no duplicate");
  canvas.stopDrawingMode();
  canvas.clearAll();

  // Plain clicks still build one connected chain.
  canvas.startDrawingMode();
  click(canvas, {10, 10});
  click(canvas, {100, 10});
  click(canvas, {100, 100});
  check(canvas.getLines().empty() && canvas.getCurrentLine().points.size() == 3,
        "plain clicks keep connecting");
  canvas.stopDrawingMode();

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

// The wiring on the real widget: a click paints the vertex ON ITS WAY, an export never does, and
// the resting picture is what is left behind. Needs a live CanvasWidget, so it sits apart.
#include "CanvasWidget.hpp"
#include "motionPrefs.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <cmath>

#include "support/check.hpp"

// Paint the widget into an image the way the screen would (offscreen platform).
static QImage shot(QWidget& w) {
  QImage out(w.size(), QImage::Format_ARGB32);
  out.fill(Qt::transparent);
  QPainter p(&out);
  w.render(&p);
  return out;
}

// Is there ink like `c` within `rad` px of (x, y)? The stroke is antialiased and the
// effects tint it, so an exact-colour match would miss the very pixels that matter.
static bool inkNear(const QImage& img, int x, int y, const QColor& c, int rad = 6) {
  for (int dy = -rad; dy <= rad; ++dy) {
    for (int dx = -rad; dx <= rad; ++dx) {
      const int px = x + dx, py = y + dy;
      if (px < 0 || py < 0 || px >= img.width() || py >= img.height()) continue;
      const QColor got = img.pixelColor(px, py);
      if (std::abs(got.red() - c.red()) < 60 && std::abs(got.green() - c.green()) < 60 &&
          std::abs(got.blue() - c.blue()) < 60)
        return true;
    }
  }
  return false;
}

// Let the widget's own frame timer run for a while.
static void spin(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

void canvasWiring() {
  const QColor ink(QStringLiteral("#e11d48"));
  stencil::gui::CanvasWidget canvas;
  QImage blank(400, 300, QImage::Format_RGB32);
  blank.fill(Qt::white);
  canvas.loadFromImage(blank);
  canvas.setDefaults(QStringLiteral("#e11d48"), 5.0, 9.0, QStringLiteral("solid"),
                     QStringLiteral("#e11d48"));
  canvas.setScale(1.0);
  canvas.resize(canvas.imageWidth(), canvas.imageHeight());
  canvas.startDrawingMode();

  const QPoint anchor(40, 40);
  const QPoint target(340, 250);
  const auto click = [&](const QPoint& at) {
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(at), canvas.mapToGlobal(at),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &press);
    QMouseEvent rel(QEvent::MouseButtonRelease, QPointF(at), canvas.mapToGlobal(at),
                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &rel);
  };
  click(anchor);
  spin(700);                       // let the first point settle
  click(target);
  // A moment into the flight, not at the very start of it: at t=0 the vertex is still on
  // the anchor and the segment has no length yet, so there would be nothing to see leaving.
  spin(90);
  const QImage flying = shot(canvas);
  check(canvas.currentLine().points.size() == 2, "the click really added the point");
  check(!inkNear(flying, target.x(), target.y(), ink, 8),
        "mid-flight the vertex has not reached the point you clicked");
  check(inkNear(flying, anchor.x() + 30, anchor.y() + 21, ink, 10),
        "…and the segment is already leaving the anchor");

  // The export is the resting picture even while the vertex is still in the air.
  const QImage exported = canvas.renderToImage(QStringLiteral("current"));
  check(inkNear(exported, target.x(), target.y(), ink, 8),
        "an export never bakes a vertex mid-air");

  spin(900);                       // everything has landed
  const QImage settled = shot(canvas);
  check(inkNear(settled, target.x(), target.y(), ink, 8), "the vertex ends where it was put");
  check(inkNear(settled, (anchor.x() + target.x()) / 2, (anchor.y() + target.y()) / 2, ink, 10),
        "…joined to the point it came from");

  // "Drawing animation" off: the vertex goes straight down. The Visuals switch (support/motionPrefs.hpp;
  // browser stencil.drawingAnimations) is asked before any vertex is sent, so nothing is ever in the air.
  stencil::support::setDrawingAnimations(false);
  const QPoint straight(120, 265);
  click(straight);
  check(inkNear(shot(canvas), straight.x(), straight.y(), ink, 8),
        "with the animation off the vertex is where you clicked at once");
  stencil::support::setDrawingAnimations(true);
}

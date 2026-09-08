// Headless check of the stroke-growth flight (src/canvas/strokeGrowth.hpp) — the desktop
// port of browser/js/ui/motion.js + js/core/strokeFx.js. The painting needs a live widget;
// the arithmetic saying where a just-added vertex IS at a given instant does not, so that
// is what is pinned here, with the numbers that must equal the browser's and the flight
// bookkeeping. Pure QtCore geometry; no display needed.
#include "strokeGrowth.hpp"

#include "canvasWidget.hpp"
#include "motionPrefs.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <cmath>
#include <cstdio>

#include "support/check.hpp"

namespace fx = stencil::gui::stroke;
namespace core = stencil::core;

static bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) < eps; }

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

int main(int argc, char** argv) {
  QApplication app(argc, argv);   // offscreen via QT_QPA_PLATFORM

  // ── The constants the browser shares (motion.js) ──────────────────────────
  check(near(fx::kFlyMinMs, 150.0) && near(fx::kFlyMaxMs, 420.0) && near(fx::kFlyPxPerMs, 2.4),
        "flight length matches motion.js STROKE_FLY_*");
  check(near(fx::kPopMs, 240.0) && near(fx::kPopPeak, 1.5) && near(fx::kRippleMs, 420.0),
        "landing + ring match motion.js STROKE_POP_* / STROKE_RIPPLE_MS");
  check(near(fx::kBowShare, 0.13) && near(fx::kBowMax, 22.0), "the bow matches motion.js STROKE_BOW_*");

  // ── How long a flight takes: a hop is the floor, a long reach is capped.
  check(near(fx::flyMs(0), fx::kFlyMinMs), "a zero-length hop is the floor");
  check(near(fx::flyMs(300), 275.0), "300px = 150 + 300/2.4");
  check(near(fx::flyMs(4000), fx::kFlyMaxMs), "a reach across the page is capped");
  check(fx::flyMs(80) < fx::flyMs(400), "further takes longer");

  // ── The easing: exact at both ends, and it OVERSHOOTS in between — that is what
  // makes the segment read as reaching for the point.
  check(near(fx::flyEase(0.0), 0.0) && near(fx::flyEase(1.0), 1.0), "the ease is exact at both ends");
  check(fx::flyEase(-0.3) == 0.0 && fx::flyEase(2.0) == 1.0, "the ease is clamped outside 0..1");
  double peak = 0.0;
  for (int i = 0; i <= 100; ++i) peak = std::max(peak, fx::flyEase(i / 100.0));
  check(peak > 1.0 && peak < 1.12, "it overshoots, but only just");

  // ── The path: it starts on the anchor, ends on the target, and bows off the
  // straight line in between (the same rule the dust motes follow).
  const QPointF a(10, 10), b(210, 10);
  check(fx::flyPoint(a, b, 0.0, 1.0) == a, "at t=0 the vertex is still on its anchor");
  check(fx::flyPoint(a, b, 1.0, 1.0) == b, "at t=1 it is exactly on the point you clicked");
  const QPointF mid = fx::flyPoint(a, b, 0.5, 1.0);
  check(std::fabs(mid.y() - 10.0) > 1.0, "mid-flight it is off the straight line");
  const QPointF other = fx::flyPoint(a, b, 0.5, -1.0);
  check((mid.y() - 10.0) * (other.y() - 10.0) < 0, "the sign of the bow picks the side");
  check(fx::flyPoint(a, b, 0.5, 0.0).y() == 10.0, "no bow → dead straight");
  check(near(fx::bowAmp(1000.0), fx::kBowMax), "the bow is capped on a long trip");
  check(std::fabs(fx::bowSign(31, 74)) <= 1.0, "the side is a signed unit share");
  check(fx::bowSign(31, 74) == fx::bowSign(31, 74), "…and it is a hash, so it is reproducible");

  // ── The vertex's size: it swells in flight and settles after landing — no jump
  // between the two, or the arrival reads as a redraw instead of a landing.
  check(near(fx::flyRadius(0.0), fx::kFlyR0), "it leaves small");
  check(near(fx::flyRadius(1.0), fx::kPopPeak), "…and arrives at the peak");
  check(near(fx::popScale(0.0), fx::kPopPeak), "the settle starts where the flight ended");
  check(near(fx::popScale(1.0), 1.0), "…and ends at the point's real size");

  // ── The ring and the spark: nothing at the ends, plenty in the middle.
  check(near(fx::ripple(0.0).scale, 1.0) && near(fx::ripple(1.0).alpha, 0.0),
        "the ring starts on the point and fades to nothing");
  check(fx::ripple(0.5).scale > 1.0 && fx::ripple(0.5).scale < fx::kRippleReach, "…growing on the way");
  check(near(fx::spark(0.0).alpha, 0.0) && near(fx::spark(1.0).alpha, 0.0),
        "the spark neither smudges the anchor nor the landing");
  check(fx::spark(0.5).alpha > 0.4, "…and is bright mid-trip");
  check(near(fx::wake(0.0), fx::kWakeAlpha) && near(fx::wake(1.0), 0.0),
        "the wake burns as the vertex leaves and is out once it has landed");

  // ── The timeline: the settle and the ring both start when the flight ends.
  const fx::Phase mid2 = fx::phase(50.0, 200.0);
  check(near(mid2.fly, 0.25) && mid2.land == 0.0 && !mid2.done, "mid-flight: nothing has landed yet");
  const fx::Phase landed = fx::phase(320.0, 200.0);
  check(near(landed.fly, 1.0) && near(landed.land, 0.5), "120ms after landing the settle is half done");
  check(fx::phase(200.0 + fx::kRippleMs, 200.0).done, "the record is finished once the ring has gone");
  check(near(fx::vertexScale(fx::phase(0.0, 200.0)), fx::kFlyR0), "the scale follows the same timeline");
  check(near(fx::vertexScale(fx::phase(1000.0, 200.0)), 1.0), "…and rests at 1");

  // ── A vertex inserted into a segment comes out of its own foot on it.
  const core::Point p0{0, 0}, p1{100, 0};
  check(fx::foot(p0, p1, 40, 25) == QPointF(40, 0), "the foot is the perpendicular one");
  check(fx::foot(p0, p1, -80, 5) == QPointF(0, 0), "…clamped to the segment's own ends");
  check(fx::foot(p0, p0, 9, 9) == QPointF(0, 0), "a degenerate segment is its own foot");

  // ── The flights themselves.
  core::Line line;
  line.points = {{0, 0}, {50, 0}};
  fx::Fx flights;
  check(!flights.active(), "nothing flies until something is added");
  flights.flyIn(0, line, 1, 0.0);
  check(flights.active() && flights.touches(0), "the added vertex is in the air");
  check(!flights.touches(-1), "…on its own line only");
  const fx::Flight* f = flights.at(0, line.points[1]);
  check(f && f->from == QPointF(0, 0), "an appended vertex leaves the point it extends");
  check(flights.at(0, line.points[0]) == nullptr, "the point it left is not itself flying");

  // Identity is the RESTING coordinates, so an insert that shifts every later index
  // leaves each flight on its own vertex.
  line.points.insert(line.points.begin() + 1, core::Point{20, 20});
  check(flights.at(0, line.points[2]) == f, "a splice does not move a flight off its vertex");

  // The same vertex sent again replaces its own record instead of stacking clocks.
  flights.flyIn(0, line, 2, 100.0);
  int n = 0;
  for (int i = 0; i < static_cast<int>(line.points.size()); ++i)
    if (flights.at(0, line.points[i])) ++n;
  check(n == 1, "re-adding a vertex replaces its flight");

  // A committed in-progress line keeps its flights, on its new number.
  flights.rekey(0, 7);
  check(flights.touches(7) && !flights.touches(0), "rekey follows the line to its new index");

  // Landing empties the list; the frame loop stops when it does.
  // Past the LONGEST flight plus its ring: this vertex left (20,20) for (50,0), so its own
  // flight is longer than the minimum and stepping to that would still be mid-air.
  check(!flights.step(100.0 + fx::kFlyMaxMs + fx::kRippleMs + 1.0), "a landed flight is dropped");
  check(!flights.active(), "…and the loop has nothing left to draw");

  // A rect's corners are staggered, so the shape draws itself edge by edge.
  core::Line rect;
  rect.points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
  fx::Fx staggered;
  staggered.flyInRange(3, rect, 1, 3, 0.0);
  const fx::Flight* c1 = staggered.at(3, rect.points[1]);
  const fx::Flight* c3 = staggered.at(3, rect.points[3]);
  check(c1 && c3 && c3->start > c1->start, "each corner waits for the one before it");
  check(c3->from == QPointF(10, 10), "…and leaves the corner before it");

  // A standalone rect starts at the HEAD: that first corner has nothing before it, so it
  // pops in place rather than flying backwards out of the corner after it.
  fx::Fx whole;
  whole.flyInRange(4, rect, 0, 4, 0.0);
  const fx::Flight* head = whole.at(4, rect.points[0]);
  check(head && head->from == QPointF(0, 0), "the first corner only pops");
  check(whole.at(4, rect.points[1])->from == QPointF(0, 0), "the second leaves the first");

  // ── The wiring, on the real widget: a click paints the vertex ON ITS WAY, an export
  // never does, and the resting picture is what is left behind.
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

  // ── "Drawing animation" off: the vertex goes straight down ──────────────────
  // The Visuals-dialog switch (support/motionPrefs.hpp; browser stencil.drawingAnimations)
  // is asked before any vertex is sent, so with it off nothing is ever in the air — the
  // point is at the click in the very first frame after it.
  stencil::support::setDrawingAnimations(false);
  const QPoint straight(120, 265);
  click(straight);
  check(inkNear(shot(canvas), straight.x(), straight.y(), ink, 8),
        "with the animation off the vertex is where you clicked at once");
  stencil::support::setDrawingAnimations(true);

  // The counter every check() feeds — without this the suite passed with failures in it.
  std::printf("strokeGrowth: %s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}

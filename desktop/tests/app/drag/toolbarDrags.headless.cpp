// The toolbar icons' drags (app/drag/toolbarDrags) on plain buttons over stand-in hooks: zoom −/+
// follow z0·e^(±k·d) and come back on the icon, fit steps over − and + and restores on itself, a
// canvas icon applies only on the canvas, a dialog icon opens on its drop, and the chat docks on
// a band or floats where it drops.
#include "toolbarDrags.hpp"
#include "zoomFollow.hpp"
#include "DockZonesOverlay.hpp"
#include "iconDrag.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QTest>
#include <algorithm>
#include <cmath>

#include "../../support/check.hpp"

using namespace stencil::gui;

namespace {
  void mouse(QWidget* w, QEvent::Type type, const QPoint& global, Qt::MouseButtons held) {
    const Qt::MouseButton b = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent e(type, QPointF(w->mapFromGlobal(global)), QPointF(global), b, held, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
  }
  void press(QWidget* w, const QPoint& g) { mouse(w, QEvent::MouseButtonPress, g, Qt::LeftButton); }
  void drag(QWidget* w, const QPoint& g) { mouse(w, QEvent::MouseMove, g, Qt::LeftButton); }
  void release(QWidget* w, const QPoint& g) {
    mouse(w, QEvent::MouseButtonRelease, g, Qt::NoButton);
    QApplication::processEvents();   // the drop and the cancel run a turn later
  }
  // A whole gesture: out to `to`, then released at `at`.
  void gesture(QWidget* w, const QPoint& to, const QPoint& at) {
    const QPoint from = w->mapToGlobal(w->rect().center());
    press(w, from);
    drag(w, from + QPoint(0, 30));
    drag(w, to);
    if (at != to) drag(w, at);
    release(w, at);
  }
  QPoint centreOf(const QWidget* w) { return w->mapToGlobal(w->rect().center()); }
  bool near(double a, double b) { return std::abs(a - b) <= 1e-6 * std::max(1.0, std::abs(b)); }
  int glows(const QWidget& window) {
    int n = 0;
    for (QWidget* g : window.findChildren<QWidget*>(QLatin1String(stencil::support::DROP_GLOW_NAME)))
      n += g->isVisible() ? 1 : 0;
    return n;
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  check(zoomFromDistance(2.0, 0, 1) == 2.0, "on the icon's centre the zoom is z0");
  check(std::abs(zoomFromDistance(1.0, 300, 1) - 8.0) < 1e-9, "300 px out from + zooms ×8");
  check(std::abs(zoomFromDistance(1.0, 300, -1) - 0.125) < 1e-9, "…and from − ÷8");
  check(near(holdZoomStep(1.0, 1), 1.05) && near(holdZoomStep(1.0, -1), 0.95), "a fit step is one hold-zoom step");

  QWidget window;
  window.resize(640, 420);
  const auto button = [&window](const QString& text, int x) {
    auto* b = new QPushButton(text, &window);
    b->setGeometry(x, 20, 28, 28);
    return b;
  };
  QPushButton *out = button("-", 20), *in = button("+", 52), *fit = button("F", 84);
  QPushButton *apply = button("R", 116), *dialog = button("D", 148), *chat = button("C", 180);
  auto* canvas = new QWidget(&window);
  canvas->setGeometry(120, 140, 360, 220);
  window.show();
  const QPoint away = window.mapToGlobal(QPoint(600, 80));   // on no target and no band

  double z = 1.0;
  ZoomView view;
  view.zoom = [&z] { return z; };
  view.hold = [&z] {
    ZoomHold held;
    held.at = [&z](double v) { z = std::clamp(v, 0.05, 32.0); };
    held.restore = [&z, was = z] { z = was; };
    return held;
  };
  const DragBegin yes = [] { return true; };
  installZoomDrag(in, yes, +1, view);
  installZoomDrag(out, yes, -1, view);
  installFitDrag(fit, out, in, yes, view);

  const QPoint mid = in->mapToGlobal(QRectF(in->rect()).center()).toPoint();   // 28 px: a whole point
  press(in, mid);
  drag(in, mid + QPoint(0, 150));
  check(near(z, std::exp(ZOOM_DRAG_K * 150)), "150 px from + the zoom is z0·e^(k·150)");
  drag(in, mid + QPoint(40, 0));
  check(near(z, std::exp(ZOOM_DRAG_K * 40)), "moving back toward the icon brings it back toward z0");
  release(in, mid + QPoint(40, 0));
  check(near(z, std::exp(ZOOM_DRAG_K * 40)), "released away, the zoom stays");

  const double kept = z;
  gesture(out, centreOf(out) + QPoint(200, 120), centreOf(out));
  check(z == kept, "released back on −, the zoom from before the press returns");
  press(in, centreOf(in));
  drag(in, centreOf(in) + QPoint(0, 2000));
  check(z == 32.0, "a long drag is held to the zoom range");
  QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QApplication::sendEvent(&window, &esc);
  QApplication::processEvents();
  check(z == kept, "Escape restores it too");
  release(in, centreOf(in) + QPoint(0, 2000));

  z = 1.0;
  press(fit, centreOf(fit));
  drag(fit, centreOf(fit) + QPoint(0, 60));
  check(glows(window) == 2, "a fit drag lights − and + as its targets");
  drag(fit, centreOf(in));
  check(near(z, 1.0 + HOLD_ZOOM_STEP), "arriving on + steps the zoom in at once");
  QTest::qWait(HOLD_ZOOM_TICK_MS * 2 + 45);
  check(z >= 1.0 + 2 * HOLD_ZOOM_STEP - 1e-9, "…and on at the hold-zoom rate while it rests there");
  const double stepped = z;
  drag(fit, centreOf(out));
  QTest::qWait(HOLD_ZOOM_TICK_MS * 2 + 45);
  check(z < stepped, "…and on − steps it out");
  drag(fit, centreOf(fit));
  release(fit, centreOf(fit));
  check(z == 1.0 && glows(window) == 0, "released on fit, the drag's starting zoom returns and the glows go");
  press(fit, centreOf(fit));
  drag(fit, centreOf(fit) + QPoint(0, 60));
  drag(fit, centreOf(in));
  QTest::qWait(HOLD_ZOOM_TICK_MS * 2 + 45);
  release(fit, centreOf(in));
  const double held = z;
  QTest::qWait(HOLD_ZOOM_TICK_MS * 2);
  check(held > 1.0 && z == held && glows(window) == 0, "released anywhere else, the zoom stays and stepping stops");

  int applied = 0;
  installCanvasDrag(apply, yes, [canvas] { return canvas; }, [&applied] { ++applied; });
  press(apply, centreOf(apply));
  drag(apply, centreOf(apply) + QPoint(0, 40));
  check(glows(window) == 1, "the canvas glows as the drop target");
  drag(apply, centreOf(canvas));
  release(apply, centreOf(canvas));
  check(applied == 1 && glows(window) == 0, "dropped on the canvas it applies once, and the glow goes");
  gesture(apply, away, away);
  check(applied == 1, "dropped anywhere else nothing happens");

  QPoint opened;
  installDialogDrag(dialog, yes, [&opened](const QPoint& at) { opened = at; });
  gesture(dialog, centreOf(canvas), centreOf(canvas));
  check(opened == centreOf(canvas), "a dialog icon dropped away opens on the drop point");
  opened = QPoint();
  gesture(dialog, centreOf(canvas), centreOf(dialog));
  check(opened.isNull(), "released back on the icon, nothing opens");

  auto* zones = new DockZonesOverlay(&window);
  Qt::DockWidgetArea docked = Qt::NoDockWidgetArea;
  QPoint floated;
  ChatPlacing placing;
  placing.showZones = [zones, &window](std::function<bool()> live) {
    zones->beginDrag(Qt::blue, window.rect(), std::move(live));
  };
  placing.zones = [zones] { return zones; };
  placing.dock = [&docked](Qt::DockWidgetArea a) { docked = a; };
  placing.floatAt = [&floated](const QPoint& at) { floated = at; };
  installChatDrag(chat, yes, placing);
  const QPoint leftBand = window.mapToGlobal(QPoint(30, 250));
  press(chat, centreOf(chat));
  drag(chat, centreOf(chat) + QPoint(0, 40));
  check(zones->isVisible(), "a chat drag shows the dock bands");
  drag(chat, leftBand);
  release(chat, leftBand);
  check(docked == Qt::LeftDockWidgetArea && floated.isNull() && !zones->isVisible(),
        "dropped on the left band the chat docks left, and the bands go");
  docked = Qt::NoDockWidgetArea;
  gesture(chat, centreOf(canvas), centreOf(canvas));
  check(docked == Qt::NoDockWidgetArea && floated == centreOf(canvas), "dropped off every band it floats there");
  zones->beginDrag(Qt::blue, window.rect(), [] { return true; });
  check(zones->zoneAt(window.mapToGlobal(QPoint(72, 250))) == 0 && zones->zoneAt(window.mapToGlobal(QPoint(73, 250))) < 0 &&
            zones->zoneAt(window.mapToGlobal(QPoint(320, 420 - 72))) == 3,
        "a band reaches the browser's DOCK_ZONE_BAND (72 px) from its edge, no further");
  check(zones->zoneAt(window.mapToGlobal(QPoint(30, 30))) == 0, "…and in a corner the side band wins a tie, as DOCK_SIDES orders");
  zones->hide();
  floated = QPoint();
  gesture(chat, leftBand, centreOf(chat));
  check(docked == Qt::NoDockWidgetArea && floated.isNull() && !zones->isVisible(),
        "released back on the icon it stays where it was");

  auto* refused = button("X", 212);
  installCanvasDrag(refused, [] { return false; }, [canvas] { return canvas; }, [&applied] { ++applied; });
  gesture(refused, centreOf(canvas), centreOf(canvas));
  check(applied == 1 && glows(window) == 0, "a refused start drags nothing");

  return failures ? 1 : 0;
}

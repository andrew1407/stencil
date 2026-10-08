// Dragging a toolbar control (support/drag/iconDrag, browser twin ui/drag/iconDrag.js) on a live
// button: a press inside the slop still clicks, a drag away drops and never clicks, a release back
// over the button cancels, Escape cancels and leaves the press spent, a refused start stays a press,
// a double-click's second press drags as a first does, a ghost wears the face its owner hands it, a
// source that is no button drags only from what its grab hook takes hold of, and no tip shows meanwhile.
#include "iconDragParts.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QWidget window;
  window.resize(400, 300);
  auto* button = new QPushButton(QStringLiteral("Zoom"), &window);
  button->setGeometry(20, 20, 60, 28);
  auto* target = new QWidget(&window);
  target->setGeometry(200, 150, 120, 100);
  window.show();

  QStringList log;
  int clicks = 0;
  bool refuse = false;
  QObject::connect(button, &QPushButton::clicked, [&] { ++clicks; });
  IconDragHooks hooks;
  hooks.start = [&](const QPoint&, const QPoint&) { log << QStringLiteral("start"); return !refuse; };
  hooks.move = [&](const IconDragPoint& p) { log << (p.overOrigin ? QStringLiteral("over") : QStringLiteral("move")); };
  hooks.drop = [&](const IconDragPoint& p) { log << (p.target == target ? QStringLiteral("drop:target") : QStringLiteral("drop")); };
  hooks.cancel = [&] { log << QStringLiteral("cancel"); };
  installIconDrag(button, hooks);
  const int slop = uiTimings().pressSlopPx;
  const QPoint in(30, 14);
  const QPoint onTarget = button->mapFrom(&window, QPoint(260, 200));

  press(button, in);
  drag(button, in + QPoint(slop, 0));
  release(button, in + QPoint(slop, 0));
  app.processEvents();
  check(clicks == 1 && log.isEmpty(), "a press that stays inside the slop is a click");

  press(button, in);
  drag(button, in + QPoint(0, slop + 2));
  check(iconDragActive(button), "past the slop the drag is live");
  check(!button->isDown(), "…and the button no longer reads as pressed");
  drag(button, onTarget);
  release(button, onTarget);
  check(!log.contains(QStringLiteral("drop:target")), "the drop waits for the next turn");
  app.processEvents();
  check(log.contains(QStringLiteral("drop:target")), "released over a widget, the drop names it");
  check(clicks == 1, "a drag never clicks");

  log.clear();
  press(button, in);
  drag(button, onTarget);
  drag(button, in);
  release(button, in);
  app.processEvents();
  check(log.last() == QStringLiteral("cancel") && !log.join(',').contains(QStringLiteral("drop")),
        "released back over the button, the drag cancels");
  check(clicks == 1, "…and the release over it still clicks nothing");

  log.clear();
  press(button, in);
  drag(button, onTarget);
  QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QApplication::sendEvent(&window, &esc);
  app.processEvents();
  check(!iconDragActive(button) && log.last() == QStringLiteral("cancel"), "Escape cancels a live drag");
  drag(button, in);
  release(button, in);
  app.processEvents();
  check(clicks == 1 && log.count(QStringLiteral("cancel")) == 1, "…and the press stays spent after it");

  log.clear();
  refuse = true;
  press(button, in);
  drag(button, in + QPoint(slop + 4, 0));
  drag(button, onTarget);
  check(!iconDragActive(button) && log == QStringList{QStringLiteral("start")}, "a refused start is asked once");
  release(button, onTarget);
  app.processEvents();
  check(clicks == 1, "…and the press runs its own course: released off the button, no click");

  // A quick click, then a drag: the drag's press reaches the button as a double-click's second.
  log.clear();
  refuse = false;
  press(button, in);
  release(button, in);
  app.processEvents();
  mouse(button, QEvent::MouseButtonDblClick, in, Qt::LeftButton);
  drag(button, in + QPoint(0, slop + 2));
  check(iconDragActive(button), "a double-click's second press drags as a first does");
  drag(button, onTarget);
  release(button, onTarget);
  app.processEvents();
  check(log.contains(QStringLiteral("drop:target")) && clicks == 2, "…and drops, its release clicking nothing");

  sourceCases(app, window, in, slop, hooks, log);

  const unsigned before = dragsStarted();
  press(button, in);
  drag(button, in + QPoint(slop, 0));
  release(button, in + QPoint(slop, 0));
  check(dragsStarted() == before, "a press inside the slop counts as no drag");
  press(button, in);
  drag(button, onTarget);
  drag(button, onTarget + QPoint(5, 5));
  release(button, onTarget);
  app.processEvents();
  check(dragsStarted() == before + 1, "a started drag counts once, however far it goes");

  // No tip shows while a drag is live: those up as it starts go, and none comes until it ends.
  stencil::gui::AppTooltip* tip = stencil::gui::appTooltip();
  button->setToolTip(QStringLiteral("Zoom"));
  target->setToolTip(QStringLiteral("Target"));
  QWidget ownTip(nullptr, Qt::ToolTip);
  ownTip.setProperty(stencil::gui::TIP_WINDOW_PROPERTY, true);
  ownTip.resize(40, 20);
  const auto ask = [](QWidget* w, const QPoint& local) {
    QHelpEvent help(QEvent::ToolTip, local, w->mapToGlobal(local));
    QApplication::sendEvent(w, &help);
  };
  const auto tipUp = [tip] { return tip->isVisible() && !tip->fadingOut(); };
  press(button, in);
  ask(button, in);
  ownTip.show();
  const bool upBefore = tipUp() && ownTip.isVisible();
  drag(button, onTarget);
  check(upBefore && !tipUp() && !ownTip.isVisible(), "a drag's start takes every tip down");
  ask(target, QPoint(10, 10));
  ask(button, in);
  check(!tipUp(), "…none shows while it lasts");
  release(button, onTarget);
  app.processEvents();
  ask(target, QPoint(10, 10));
  check(tipUp(), "…and they come back once it ends");

  return failures ? 1 : 0;
}

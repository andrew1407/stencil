// Dragging a toolbar control (support/drag/iconDrag, browser twin ui/drag/iconDrag.js) on a live
// button: a press inside the slop still clicks, a drag away drops and never clicks, a release back
// over the button cancels, Escape cancels and leaves the press spent, a refused start stays a press,
// a double-click's second press drags as a first does, a ghost wears the face its owner hands it, a
// source that is no button drags only from what its grab hook takes hold of, and no tip shows meanwhile.
#include "iconDrag.hpp"
#include "uiTimings.hpp"
#include "AppTooltip.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QStringList>
#include <QWidget>

#include "../check.hpp"

using namespace stencil::support;

namespace {
  void mouse(QWidget* w, QEvent::Type type, const QPoint& local, Qt::MouseButtons held) {
    const Qt::MouseButton b = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent e(type, QPointF(local), QPointF(w->mapToGlobal(local)), b, held, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
  }
  void press(QWidget* w, const QPoint& at) { mouse(w, QEvent::MouseButtonPress, at, Qt::LeftButton); }
  void drag(QWidget* w, const QPoint& at) { mouse(w, QEvent::MouseMove, at, Qt::LeftButton); }
  void release(QWidget* w, const QPoint& at) { mouse(w, QEvent::MouseButtonRelease, at, Qt::NoButton); }
}  // namespace

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

  // A control painted from elsewhere (the header mark) hands the ghost its picture.
  auto* marked = new QPushButton(QStringLiteral("Mark"), &window);
  marked->setGeometry(100, 20, 60, 28);
  marked->show();
  QPixmap face(40, 40);
  face.fill(Qt::red);
  IconDragHooks faced;
  faced.face = [&face] { return face; };
  installIconDrag(marked, faced);
  const auto ghostOf = [&window](const QSize& size) {
    for (QWidget* child : window.findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
      if (child->testAttribute(Qt::WA_TransparentForMouseEvents) && child->isVisible() && child->size() == size)
        return true;
    return false;
  };
  const QPoint away = marked->mapFrom(&window, QPoint(260, 200));
  press(marked, in);
  drag(marked, away);
  check(ghostOf(QSize(40, 40)), "a ghost wears the face its owner hands it");
  release(marked, away);
  app.processEvents();
  check(!ghostOf(QSize(40, 40)), "…and leaves with the drag");

  // The theme lens rides its switch in the circle's middle, wherever the press took hold of it.
  auto* centred = new QPushButton(QStringLiteral("Lens"), &window);
  centred->setGeometry(180, 20, 60, 28);
  centred->show();
  IconDragHooks mid;
  mid.ghostCentred = true;
  installIconDrag(centred, mid);
  const QPoint lensAt = window.mapToGlobal(QPoint(300, 220));
  press(centred, QPoint(4, 4));
  drag(centred, centred->mapFromGlobal(lensAt));
  QWidget* riding = nullptr;
  for (QWidget* child : window.findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
    if (child->testAttribute(Qt::WA_TransparentForMouseEvents) && child->isVisible() && child->size() == centred->size())
      riding = child;
  check(riding && riding->geometry().center() == window.mapFromGlobal(lensAt),
        "a centred ghost sits centred on the pointer, not held at the grab point");
  release(centred, centred->mapFromGlobal(lensAt));
  app.processEvents();

  // A source that is no button drags from what its grab hook takes hold of, that spot the origin,
  // and never sees the live drag's release.
  auto* strip = new QWidget(&window);
  strip->setGeometry(20, 260, 200, 30);
  strip->show();
  int stripReleases = 0;
  struct Releases : QObject {
    int& seen;
    explicit Releases(int& count) : seen(count) {}
    bool eventFilter(QObject*, QEvent* e) override { seen += e->type() == QEvent::MouseButtonRelease; return false; }
  } counter(stripReleases);
  strip->installEventFilter(&counter);
  IconDragHooks spots = hooks;
  spots.grab = [strip](const QPoint& g) {   // only the left half of the strip is a handle
    const QRect half(strip->mapToGlobal(QPoint(0, 0)), QSize(strip->width() / 2, strip->height()));
    return half.contains(g) ? half : QRect();
  };
  installIconDrag(strip, spots);
  log.clear();
  press(strip, QPoint(150, 15));
  drag(strip, QPoint(150, 15 - slop - 4));
  release(strip, QPoint(150, 15 - slop - 4));
  app.processEvents();
  check(log.isEmpty() && stripReleases == 1, "a press its grab hook declines drags nothing");
  press(strip, QPoint(20, 15));
  drag(strip, QPoint(150, 15));
  release(strip, QPoint(150, 15));
  app.processEvents();
  check(log.contains(QStringLiteral("drop")) && stripReleases == 1,
        "released on the same source off the grabbed spot it drops, the source never seeing the release");
  log.clear();
  press(strip, QPoint(20, 15));
  drag(strip, QPoint(20, 15 + slop + 4));
  release(strip, QPoint(30, 12));
  app.processEvents();
  check(log.last() == QStringLiteral("cancel"), "…and back on the grabbed spot it cancels");

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

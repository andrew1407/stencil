// No tip through a control drag (support/drag/iconDrag over support/tip/AppTooltip): as the drag
// starts the app's tooltip goes at once with the dust it threw, none shows until the drag ends, and
// the start hook already sees a window with neither; afterwards tips show again.
#include "AppTooltip.hpp"
#include "iconDrag.hpp"
#include "uiTimings.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <QPushButton>
#include <QWidget>

#include "../check.hpp"

using namespace stencil::support;
using stencil::gui::AppTooltip;

namespace {
  void mouse(QWidget* w, QEvent::Type type, const QPoint& local, Qt::MouseButtons held) {
    const Qt::MouseButton b = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent e(type, QPointF(local), QPointF(w->mapToGlobal(local)), b, held, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
  }

  // A tip up on `owner`, its gathering dust still flying over the window.
  void tipOn(AppTooltip* tip, QWidget* owner) {
    tip->showFor(owner, QStringLiteral("Zoom in"), owner->mapToGlobal(owner->rect().center()));
  }
}  // namespace

int main(int argc, char** argv) {
  qunsetenv("STENCIL_NO_ANIM");   // the dust is motion: it must be on to be caught
  QApplication app(argc, argv);
  QWidget window;
  window.resize(400, 300);
  auto* button = new QPushButton(QStringLiteral("Zoom"), &window);
  button->setGeometry(20, 20, 120, 40);
  window.show();
  stencil::gui::installAppTooltips();
  AppTooltip* tip = stencil::gui::appTooltip();
  check(tip != nullptr, "the app tooltip is installed");
  if (!tip) return failures ? 1 : 0;

  bool cleanAtStart = false;
  IconDragHooks hooks;
  hooks.start = [&](const QPoint&, const QPoint&) {
    cleanAtStart = !tip->isVisible() && !tip->liveDust();
    return true;
  };
  installIconDrag(button, hooks);
  const QPoint in(30, 20);
  const QPoint away = button->mapFrom(&window, QPoint(300, 220));

  tipOn(tip, button);
  check(tip->isVisible(), "a tip is up on the control");
  mouse(button, QEvent::MouseButtonPress, in, Qt::LeftButton);
  mouse(button, QEvent::MouseMove, in + QPoint(0, uiTimings().pressSlopPx + 4), Qt::LeftButton);
  check(iconDragActive(button), "the drag is live");
  check(cleanAtStart, "the start hook already sees no tip and no dust");
  check(!tip->isVisible() && !tip->liveDust(), "the tip went at once, its dust with it");
  tipOn(tip, button);
  check(!tip->isVisible() && !tip->liveDust(), "no tip shows while the drag lives");
  mouse(button, QEvent::MouseMove, away, Qt::LeftButton);
  mouse(button, QEvent::MouseButtonRelease, away, Qt::NoButton);
  app.processEvents();
  tipOn(tip, button);
  check(tip->isVisible(), "the drag over, tips show again");
  return failures ? 1 : 0;
}

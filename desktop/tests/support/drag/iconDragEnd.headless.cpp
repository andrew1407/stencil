// A drag's end as fullscreen needs it (support/drag/iconDrag) and the ghost's shining rim
// (dragOverlays): afterIconDrag waits for the drop; a source hidden mid-drag (a folding fullscreen
// row) still drops where it is released.
#include "iconDragParts.hpp"
#include "dragOverlays.hpp"

#include <QTest>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QWidget window;
  window.resize(400, 300);
  window.show();
  for (const bool still : {true, false})
    check(ghostShine(0, still) >= 0 && ghostShine(700, still) <= 1, "the ghost's rim breathes inside 0..1");
  check(ghostShine(0, true) == 1 && ghostShine(0, false) == 0 && ghostShine(700, false) == 1,
        "…from dim to full over a beat, and held full when motion is reduced");

  auto* button = new QPushButton(QStringLiteral("Zoom"), &window);
  button->setGeometry(20, 20, 60, 28);
  button->show();
  QStringList log;
  IconDragHooks hooks;
  hooks.drop = [&](const IconDragPoint&) { log << QStringLiteral("drop"); };
  installIconDrag(button, hooks);
  const QPoint in(30, 14), away = button->mapFrom(&window, QPoint(260, 200));
  afterIconDrag([&] { log << QStringLiteral("idle"); });
  check(log == QStringList{QStringLiteral("idle")} && !anyIconDragActive(), "with no drag it runs at once");
  log.clear();
  press(button, in);
  drag(button, away);
  check(anyIconDragActive(), "a live drag is seen app-wide");
  afterIconDrag([&] { log << QStringLiteral("after"); });
  button->hide();
  mouse(&window, QEvent::MouseButtonRelease, QPoint(260, 200), Qt::NoButton);
  check(log.isEmpty(), "…what waits on it waits for the drop");
  app.processEvents();
  app.processEvents();
  check(log == (QStringList{QStringLiteral("drop"), QStringLiteral("after")}),
        "a source hidden mid-drag still drops where released, then the waiting work runs");
  check(!anyIconDragActive() && !iconDragActive(button), "…and the drag is over");

  return failures ? 1 : 0;
}

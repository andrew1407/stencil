// The Projects window's header while a row is held (dialogs/projects/list/ProjectDragMenu, browser twins
// ui/projects/list/dragMenu.js and dragClose.js), over a live header and a stand-in row menu: the ⋯
// forms right after the title, opens the menu, keeps it across the gap, lights an item without
// taking it, takes only the item released on, opens a flyout, folds when left, and arms Close.
#include "ProjectDragMenu.hpp"

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QDialog>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include "../../../support/check.hpp"

using stencil::gui::ProjectDragMenu;

namespace {
  void pumpFor(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  // The header polls the real cursor, as it does under a live drag.
  void holdAt(const QPoint& global, int ms = 60) {
    QCursor::setPos(global);
    pumpFor(ms);
  }
  QAction* named(QMenu* m, const char* text) {
    for (QAction* a : m->actions())
      if (a->text() == QLatin1String(text)) return a;
    return nullptr;
  }
  QPoint rowOf(QMenu* m, QAction* a) { return m->mapToGlobal(m->actionGeometry(a).center()); }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QDialog dlg;
  dlg.resize(560, 420);
  auto* outer = new QVBoxLayout(&dlg);
  auto* header = new QWidget(&dlg);
  auto* row = new QHBoxLayout(header);
  auto* title = new QLabel(QStringLiteral("Projects"), header);
  auto* pill = new QPushButton(QStringLiteral("Close"), header);
  row->addWidget(title);
  row->addStretch(1);
  row->addWidget(pill);
  outer->addWidget(header);
  outer->addStretch(1);
  dlg.show();

  ProjectDragMenu drag(title, pill, &dlg);
  QMenu* opened = nullptr;
  QPoint dustFrom;
  drag.openMenu = [&](const QPoint& at, const QPoint& from) {
    dustFrom = from;
    opened = new QMenu(&dlg);
    opened->addAction(QStringLiteral("Open"));
    opened->addMenu(QStringLiteral("Make a copy"))->addAction(QStringLiteral("Image only"));
    opened->addAction(QStringLiteral("Remove"));
    opened->popup(at);
    return opened;
  };
  const int rest = 600;   // longer than any hover a menu could act on
  QToolButton* more = drag.button();
  const QPoint farAway = dlg.mapToGlobal(QPoint(500, 380));

  std::printf("the ⋯ and its menu:\n");
  check(more && !more->isVisible() && more->focusPolicy() == Qt::NoFocus, "the ⋯ waits hidden and is never a tab stop");
  check(row->indexOf(more) == row->indexOf(title) + 1, "…right after the title");
  holdAt(farAway);
  drag.begin(false);
  check(more->isVisible() && more->width() > 0 && more->x() >= title->geometry().right(),
        "a held row brings it, placed after the title before its reveal measures it");
  pumpFor(30);
  holdAt(more->mapToGlobal(more->rect().center()));
  check(opened && opened->isVisible(), "held over the ⋯, the row menu pops up");
  check(opened && opened->geometry().top() >= more->mapToGlobal(QPoint(0, more->height())).y(),
        "…under the ⋯");
  check(dustFrom == more->mapToGlobal(more->rect().center()),
        "…its dust out of the ⋯'s centre, not the corner it is placed at");
  holdAt(more->mapToGlobal(QPoint(more->width() / 2, more->height() + 3)));
  check(opened && opened->isVisible(), "the gap between the ⋯ and its menu is no way out");
  QMenu* menu = opened;
  holdAt(rowOf(menu, named(menu, "Open")));
  check(menu->activeAction() == named(menu, "Open"), "the item under the held row is lit");
  holdAt(rowOf(menu, named(menu, "Remove")), rest);
  check(menu->activeAction() == named(menu, "Remove"), "the light follows the row");
  check(!drag.claimed() && menu->isVisible(), "resting on an item takes nothing");
  ProjectDragMenu::Release out = drag.finish(QCursor::pos());
  check(out.taken == named(menu, "Remove") && out.menu == menu, "released there, it is taken with its menu, to run");
  check(drag.claimed() && !menu->isVisible(), "…the release is the header's and the menu folds");
  check(!out.close && !more->isVisible(), "the ⋯ left with the drag");
  delete out.menu.data();

  std::printf("a flyout:\n");
  holdAt(farAway);
  drag.begin(false);
  holdAt(more->mapToGlobal(more->rect().center()));
  menu = opened;
  QAction* copy = named(menu, "Make a copy");
  holdAt(rowOf(menu, copy), rest);
  check(!drag.claimed() && copy->menu()->isVisible(), "an opener opens its list on hover");
  holdAt(rowOf(copy->menu(), copy->menu()->actions().first()));
  out = drag.finish(QCursor::pos());
  check(out.taken == copy->menu()->actions().first(), "a release on the list's own item takes it");
  delete out.menu.data();

  std::printf("leaving, and dropping:\n");
  holdAt(farAway);
  drag.begin(false);
  holdAt(more->mapToGlobal(more->rect().center()));
  QPointer<QMenu> left = opened;
  holdAt(farAway, rest);
  check(!drag.claimed() && (!left || !left->isVisible()), "leaving both folds the menu and takes nothing");
  out = drag.finish(farAway);
  check(!out.taken && !drag.claimed(), "a release far off is not the header's");
  drag.begin(false);
  holdAt(more->mapToGlobal(more->rect().center()));
  menu = opened;
  QCursor::setPos(rowOf(menu, named(menu, "Open")));
  out = drag.finish(QCursor::pos());
  check(out.taken == named(menu, "Open"), "a drop on an item takes it");
  delete out.menu.data();
  drag.begin(false);
  holdAt(more->mapToGlobal(more->rect().center()));
  out = drag.finish(more->mapToGlobal(more->rect().center()));
  check(!out.taken && drag.claimed(), "a drop on the ⋯ is the header's, and takes nothing");

  std::printf("Close:\n");
  const QPoint onPill = pill->mapToGlobal(pill->rect().center());
  holdAt(farAway);
  drag.begin(false);
  check(!drag.closeArmed(), "another project leaves Close unarmed");
  holdAt(onPill);
  out = drag.finish(onPill);
  check(!out.close && !drag.claimed(), "…and its drop there is nobody's");
  drag.begin(true);
  check(drag.closeArmed(), "the project open here arms Close");
  holdAt(onPill);
  check(drag.track(onPill), "over it the drop is Close's");
  out = drag.finish(onPill);
  check(out.close && drag.claimed() && !out.taken, "dropped there, it asks to close");

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}

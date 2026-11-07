// Headless checks for support/menu/menuReveal.hpp's trigger rules (browser twins:
// projects/window/projectRowMenu.js openAnchor, contextMenu/nav.js subPoint): a press on the ⋯ that
// opened a menu closes it without replaying onto the ⋯, a press elsewhere still replays, and a
// flyout grows out of its row's ▸, not the row's middle.
#include "../../../src/support/menu/menuReveal.hpp"

#include <QApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QTest>

#include "../check.hpp"

using namespace stencil::support;

namespace {
  void pressAt(QMenu& menu, const QPoint& global) {
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(menu.mapFromGlobal(global)), QPointF(global),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&menu, &press);
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QWidget host;
  host.setGeometry(50, 50, 400, 300);
  auto* more = new QPushButton(QStringLiteral("⋯"), &host);
  more->setGeometry(10, 10, 24, 24);
  host.show();
  QTest::qWaitForWindowExposed(&host);
  const QRect moreGlobal(more->mapToGlobal(QPoint(0, 0)), more->size());

  QMenu menu(&host);
  menu.addAction(QStringLiteral("Rename"));
  menu.addAction(QStringLiteral("Delete"));
  revealMenuFrom(menu, more);
  check(menu.findChild<QObject*>(QStringLiteral("stencilTriggerPressGuard")) != nullptr,
        "a menu hung off a control is guarded against its own trigger's press");

  menu.popup(more->mapToGlobal(QPoint(0, more->height() + 200)));
  QApplication::processEvents();
  check(menu.isVisible(), "the menu opens");
  pressAt(menu, moreGlobal.center());
  check(!menu.isVisible(), "a press on the ⋯ closes it");
  check(menu.testAttribute(Qt::WA_NoMouseReplay), "…and is not replayed onto the ⋯, whose click would reopen it");

  menu.popup(more->mapToGlobal(QPoint(0, more->height() + 200)));
  QApplication::processEvents();
  check(!menu.testAttribute(Qt::WA_NoMouseReplay), "a fresh open forgets the last press");
  pressAt(menu, host.mapToGlobal(QPoint(300, 20)));
  check(!menu.isVisible(), "a press elsewhere closes it too");
  check(!menu.testAttribute(Qt::WA_NoMouseReplay), "…and still reaches what is under it");

  // A menu a right-click opened carries no guard: the ⋯ opens its own over it.
  QMenu pointMenu(&host);
  pointMenu.addAction(QStringLiteral("Copy"));
  revealMenu(pointMenu, host.mapToGlobal(QPoint(200, 150)));
  check(pointMenu.findChild<QObject*>(QStringLiteral("stencilTriggerPressGuard")) == nullptr,
        "a point-opened menu lets every press through");

  QMenu parent(&host);
  parent.addAction(QStringLiteral("Open"));
  auto* sub = new QMenu(QStringLiteral("Make a copy"), &parent);
  sub->addAction(QStringLiteral("Layout"));
  QAction* opener = parent.addMenu(sub);
  parent.popup(host.mapToGlobal(QPoint(100, 60)));
  QApplication::processEvents();
  const QRect row = parent.actionGeometry(opener);
  const QPoint rowGlobalRight = parent.mapToGlobal(QPoint(row.right(), row.center().y()));
  const QPoint arrow = submenuArrowPoint(parent, opener);
  check(arrow.y() == rowGlobalRight.y(), "the flyout's origin is on the row's middle line");
  check(arrow.x() < rowGlobalRight.x() && arrow.x() >= rowGlobalRight.x() - 14,
        "…at the ▸ just inside the row's right edge, not the row's centre");
  check(submenuArrowPoint(parent, nullptr).isNull(), "no row, no point");
  parent.hide();
  return failures ? 1 : 0;
}

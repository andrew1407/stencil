// A checkable menu row's mark dusts like a checkbox's (support/control/swap/menuCheckSwap.hpp), in
// the open menu itself; an exclusive group dusts both the row it lands on and the one it leaves.
#include "controlSwapParts.hpp"
#include "menuCheckSwap.hpp"

#include <QAction>
#include <QActionGroup>
#include <QMenu>

void menuRowDust() {
  QMenu menu;
  QAction* points = menu.addAction(QStringLiteral("Show Points"));
  points->setCheckable(true);
  auto* group = new QActionGroup(&menu);
  QAction* solid = menu.addAction(QStringLiteral("Solid"));
  QAction* dashed = menu.addAction(QStringLiteral("Dashed"));
  for (QAction* a : {solid, dashed}) { a->setCheckable(true); group->addAction(a); }
  solid->setChecked(true);
  menu.popup(QPoint(40, 40));
  pumpUntil([&menu] { return menu.isVisible(); });

  stencil::gui::toggleMenuRowsWithDust(&menu, {points}, [points] { points->toggle(); });
  check(points->isChecked(), "the toggle itself still lands");
  check(liveCheckOverlays(&menu) == 1, "the row's mark forms as dust, inside the open menu");
  pumpUntil([&menu] { return liveCheckOverlays(&menu) == 0; });

  stencil::gui::toggleMenuRowsWithDust(&menu, group->actions(), [dashed] { dashed->setChecked(true); });
  check(dashed->isChecked() && !solid->isChecked(), "the radio moves");
  check(liveCheckOverlays(&menu) == 2, "…and both its old and new rows dust");
  pumpUntil([&menu] { return liveCheckOverlays(&menu) == 0; });

  menu.hide();
  stencil::gui::toggleMenuRowsWithDust(&menu, {points}, [points] { points->toggle(); });
  check(!points->isChecked() && liveCheckOverlays(&menu) == 0, "a closed menu just toggles, no dust");
}

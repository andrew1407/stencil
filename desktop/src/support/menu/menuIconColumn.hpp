#pragma once
// Tightens a menu's icon-to-label gap to the browser's 8px (.ctx-item gap). Qt already reserves
// an icon column wherever a row has an icon or a check, so the app-wide 24px left pad doubles it.
#include "skinPrefs.hpp"

#include <QAction>
#include <QMenu>

namespace stencil::support {

  // 6px past Qt's column (glyph + 4) lands the label 8px past the glyph. Menus without the
  // column keep the app pad, so a plain label is never flush with the rounded hover.
  inline void tightenIconColumn(QMenu& menu) {
    if (isWebcore()) return;
    const auto walk = [](QMenu* m, const auto& self) -> void {
      bool column = false;
      for (QAction* a : m->actions()) {
        column = column || !a->icon().isNull() || a->isCheckable();
        if (a->menu()) self(a->menu(), self);
      }
      m->setProperty("iconColumn", column);
    };
    walk(&menu, walk);
    menu.setStyleSheet(menu.styleSheet()
                       + QStringLiteral("\nQMenu[iconColumn=\"true\"]::item{padding-left:6px;}"));
  }

}  // namespace stencil::support

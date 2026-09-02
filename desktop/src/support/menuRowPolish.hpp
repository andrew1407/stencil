#pragma once
// Wire the row polish — keycap shake + hover sweep (menuHotkeys.hpp /
// menuShimmer.hpp) — onto a menu that is built ONCE and reused for the app's whole
// life (the toolbar's export-options popups, the top Data menu's Copy/Download Image
// submenus). The canvas context menu's per-right-click rebuild instead declares the
// two as stack-locals right after the menu. Per-row icon hover motion needs no
// wiring — the app-wide filter (iconMotion.hpp's QMenu branch) covers every menu.
//
// MenuShimmer touches nothing outside the menu it's given, so it's constructed once,
// parented to `menu`. MenuHotkeyChips is different: it mutates shared QAction text
// and its row-shake animation closes over a chip child of `menu` by raw pointer — so
// it must be torn down BEFORE the menu's children go, which for a long-lived menu
// means rebuilding it on every aboutToShow and resetting on every aboutToHide.
#include "menuHotkeys.hpp"
#include "menuShimmer.hpp"

#include <QMenu>
#include <QObject>

#include <memory>

namespace stencil::support {

  // `compact` is for a short, flat list of hotkey rows with no submenu children of its
  // own (the copy/download-image variant popups) — see MenuHotkeyChips's own comment.
  inline void wireMenuRowPolish(QMenu* menu, QObject* owner, bool compact = false) {
    new MenuShimmer(menu, menu);
    auto chips = std::make_shared<std::unique_ptr<MenuHotkeyChips>>();
    QObject::connect(menu, &QMenu::aboutToShow, owner,
                     [menu, chips, compact] { *chips = std::make_unique<MenuHotkeyChips>(menu, compact); });
    // aboutToHide, not Hide: this must run (and finish restoring text / tearing
    // down the shake animation) while the menu is still fully alive, exactly the
    // ordering the stack-local pattern gets for free elsewhere.
    QObject::connect(menu, &QMenu::aboutToHide, owner, [chips] { chips->reset(); });
  }

}  // namespace stencil::support

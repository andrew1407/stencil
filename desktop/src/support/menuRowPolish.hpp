#pragma once
// Row polish (MenuHotkeys.hpp / MenuShimmer.hpp) for a menu built ONCE and reused.
// MenuShimmer is parented to `menu`; MenuHotkeyChips mutates shared QAction text and
// closes over a chip child by raw pointer, so it is rebuilt on every aboutToShow and
// torn down on every aboutToHide, BEFORE the menu's children go.
#include "MenuHotkeys.hpp"
#include "MenuShimmer.hpp"

#include <QMenu>
#include <QObject>

#include <memory>

namespace stencil::support {

  // `compact` is for a short, flat list of hotkey rows with no submenus.
  inline void wireMenuRowPolish(QMenu* menu, QObject* owner, bool compact = false) {
    new MenuShimmer(menu, menu);
    auto chips = std::make_shared<std::unique_ptr<MenuHotkeyChips>>();
    QObject::connect(menu, &QMenu::aboutToShow, owner,
                     [menu, chips, compact] { *chips = std::make_unique<MenuHotkeyChips>(menu, compact); });
    // aboutToHide, not Hide: the teardown must run while the menu is still fully alive.
    QObject::connect(menu, &QMenu::aboutToHide, owner, [chips] { chips->reset(); });
  }

}  // namespace stencil::support

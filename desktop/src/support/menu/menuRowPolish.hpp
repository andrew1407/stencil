#pragma once
// Row polish (MenuHotkeys.hpp / MenuShimmer.hpp) for a menu built ONCE and reused.
// MenuShimmer is parented to `menu`; MenuHotkeyChips mutates shared QAction text and
// closes over a chip child by raw pointer, so it is rebuilt on every aboutToShow and
// torn down on every aboutToHide, BEFORE the menu's children go.
#include "MenuHotkeys.hpp"
#include "MenuShimmer.hpp"

#include <QCoreApplication>
#include <QActionEvent>
#include <QEvent>
#include <QMenu>
#include <QObject>
#include <QStyle>

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

  // A menu with no submenu row has no arrow to make room for: `flatMenu` follows its rows, and the
  // sheets give it the compact right padding (MENU_ITEM_RIGHT_PAD_PX holds the arrow's column).
  inline constexpr char FLAT_MENU_PROPERTY[] = "flatMenu";
  class FlatMenuFilter : public QObject {
   public:
    explicit FlatMenuFilter(QObject* parent) : QObject(parent) { setObjectName(QStringLiteral("stencilFlatMenus")); }
   protected:
    bool eventFilter(QObject* o, QEvent* e) override {
      const QEvent::Type t = e->type();
      if (t != QEvent::ActionAdded && t != QEvent::ActionRemoved && t != QEvent::ActionChanged) return false;
      auto* menu = qobject_cast<QMenu*>(o);
      if (!menu) return false;
      bool flat = true;
      for (const QAction* a : menu->actions())
        if (a->menu() && (t != QEvent::ActionRemoved || a != static_cast<QActionEvent*>(e)->action())) flat = false;
      if (menu->property(FLAT_MENU_PROPERTY).toBool() == flat) return false;
      menu->setProperty(FLAT_MENU_PROPERTY, flat);
      menu->style()->unpolish(menu);
      menu->style()->polish(menu);
      return false;
    }
  };
  inline void installFlatMenus() {
    QCoreApplication* app = QCoreApplication::instance();
    if (app && !app->findChild<QObject*>(QStringLiteral("stencilFlatMenus"), Qt::FindDirectChildrenOnly))
      app->installEventFilter(new FlatMenuFilter(app));
  }

}  // namespace stencil::support

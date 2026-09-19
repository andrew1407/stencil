#pragma once
// Shared ground for the MainWindow context-menu GUI suites: finding the live popup and
// opening a submenu by hover or by key. Included through MainWindow.gui.hpp's namespace.
#include "MainWindow.gui.hpp"

namespace stencil::guitest {

  inline QMenu* findMenu() {
    QMenu* menu = nullptr;
    for (int i = 0; i < 200 && !menu; ++i) {
      menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!menu) QTest::qWait(10);
    }
    return menu;
  }

  // Hover a submenu parent the way a user does and wait for its child popup, then fall back to the
  // keyboard: QTest's synthetic moves cannot drive a NATIVE popup grab (macOS).
  inline QMenu* openSub(QMenu* menu, const QString& title) {
    QAction* parent = nullptr;
    // startsWith, not ==: a submenu-opener's own hint text is native-formatted off the action it names
    // (hintTab), so a platform-specific suffix is unreliable; a hint-less label is its own prefix.
    for (QAction* a : menu->actions())
      if (a->text().startsWith(title)) parent = a;
    if (!parent || !parent->menu()) return nullptr;
    // A move to the position the cursor already occupies produces no event at all, and the first move into
    // a freshly popped menu is routinely swallowed, so nudge via a PLAIN row and retry the pair.
    for (int attempt = 0; attempt < 4 && !parent->menu()->isVisible(); ++attempt) {
      for (QAction* a : menu->actions()) {
        if (a->isSeparator() || a->menu() || !a->isEnabled()) continue;
        QTest::mouseMove(menu, menu->actionGeometry(a).center());
        break;
      }
      QTest::qWait(30);
      QTest::mouseMove(menu, menu->actionGeometry(parent).center());
      settle([&] { return !(!parent->menu()->isVisible()); }, 400);
    }
    if (!parent->menu()->isVisible()) {  // keyboard fallback
      menu->setActiveAction(parent);
      QTest::keyClick(menu, Qt::Key_Right);
      settle([&] { return !(!parent->menu()->isVisible()); }, 1000);
    }
    return parent->menu()->isVisible() ? parent->menu() : nullptr;
  }

  // Keyboard path: make the parent current and press Right — deterministic,
  // and it doubles as the "arrows/Enter still drive the menu" assertion.
  inline QMenu* openSubByKey(QMenu* menu, const QString& title) {
    QAction* parent = nullptr;
    for (QAction* a : menu->actions())    // startsWith — see openSub's comment above
      if (a->text().startsWith(title)) parent = a;
    if (!parent || !parent->menu()) return nullptr;
    menu->setActiveAction(parent);
    QTest::keyClick(menu, Qt::Key_Right);
    settle([&] { return !(!parent->menu()->isVisible()); }, 1000);
    return parent->menu()->isVisible() ? parent->menu() : nullptr;
  }

}  // namespace stencil::guitest

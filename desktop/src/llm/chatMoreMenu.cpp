#include "chatMoreMenu.hpp"

#include "../support/iconSet.hpp"

#include <QAction>
#include <QMenu>

namespace stencil::gui {

  namespace {
    constexpr int MORE_GLYPH = 14;   // browser icon('image'|'swap'|'gear', { size: 14 })
  }

  ChatMoreActions buildChatMoreMenu(QMenu& menu, bool withClear) {
    ChatMoreActions actions;
    actions.attach = menu.addAction(QStringLiteral("Add image"));
    if (withClear) actions.clear = menu.addAction(QStringLiteral("Clear history"));
    actions.swapSides = menu.addAction(QStringLiteral("Swap message sides"));
    actions.settings = menu.addAction(QStringLiteral("Settings"));
    return actions;
  }

  void restyleChatMoreMenu(const ChatMoreActions& actions, const QColor& ink) {
    if (actions.attach) actions.attach->setIcon(themedIcon("image", ink, MORE_GLYPH));
    if (actions.clear) actions.clear->setIcon(themedIcon("trash", ink, MORE_GLYPH));
    if (actions.swapSides) actions.swapSides->setIcon(themedIcon("swap", ink, MORE_GLYPH));
    if (actions.settings) actions.settings->setIcon(themedIcon("gear", ink, MORE_GLYPH));
  }

}  // namespace stencil::gui

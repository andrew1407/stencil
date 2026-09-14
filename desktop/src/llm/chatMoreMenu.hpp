#pragma once
// The "…" overflow both chat composers carry (browser js/ui/chatComposer.js). ONE builder, so
// the dock's menu and the context-menu flyout's cannot drift apart in rows, order or glyph.
#include <QColor>

class QAction;
class QMenu;

namespace stencil::gui {

  // The rows in the browser's order; `clear` is null on a host that does not offer it.
  struct ChatMoreActions {
    QAction* attach = nullptr;
    QAction* clear = nullptr;
    QAction* swapSides = nullptr;
    QAction* settings = nullptr;
  };

  // Appends the rows to `menu` (Clear history only when `withClear`). The caller connects them
  // and owns their visibility; nothing here knows what a host does with a row.
  ChatMoreActions buildChatMoreMenu(QMenu& menu, bool withClear);

  // The 14px glyph each row carries, re-inked on a theme flip.
  void restyleChatMoreMenu(const ChatMoreActions& actions, const QColor& ink);

}  // namespace stencil::gui

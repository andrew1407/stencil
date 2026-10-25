#pragma once

// What an open popup or a typed field asks of a window about to change under it.

#include <QApplication>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTextEdit>

namespace stencil::gui {

  // A modal dialog must NEVER open under a menu still holding the popup grab (it comes up behind, unfocused).
  inline void closeOpenPopupMenus() {
    for (int i = 0; i < 8; ++i) {
      auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!popup) break;
      popup->close();
    }
    // A menu popped up from an arbitrary button (not addMenu()'d into the chain, e.g. the
    // chat panel's "…") breaks Qt's own cascade-close — sweep any menu the grab loop missed.
    for (QWidget* w : QApplication::topLevelWidgets()) {
      if (auto* m = qobject_cast<QMenu*>(w); m && m->isVisible()) m->close();
    }
  }

  // Content-gated: an auto-focused empty search field must not pin its window open.
  inline bool hasTypedContentInside(QWidget* w) {
    QWidget* f = QApplication::focusWidget();
    if (!f || !w || !w->isAncestorOf(f)) return false;
    if (auto* le = qobject_cast<QLineEdit*>(f)) return !le->text().trimmed().isEmpty();
    if (auto* pe = qobject_cast<QPlainTextEdit*>(f)) return !pe->toPlainText().trimmed().isEmpty();
    if (auto* te = qobject_cast<QTextEdit*>(f)) return !te->toPlainText().trimmed().isEmpty();
    return false;
  }

}  // namespace stencil::gui

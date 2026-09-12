#pragma once
// A destructive menu row wears its colour on BOTH halves (browser .is-danger). Qt has
// no per-action colour and no QProxyStyle route once QStyleSheetStyle owns QMenu::item,
// so the row is flagged the menu's DEFAULT item and styled via `QMenu::item:default`.
#include <QAction>
#include <QColor>
#include <QMenu>
#include <QString>

namespace stencil::support {

  // Call AFTER any helper that replaces the menu's stylesheet — this appends to it.
  inline void markDangerRow(QMenu& menu, QAction* action, const QColor& danger) {
    if (!action) return;
    menu.setDefaultAction(action);
    menu.setStyleSheet(
        menu.styleSheet()
        + QStringLiteral(
              "\nQMenu::item:default{color:%1;font-weight:normal;}"
              "\nQMenu::item:default:selected{background:%1;color:#ffffff;}")
              .arg(danger.name()));
  }

}  // namespace stencil::support

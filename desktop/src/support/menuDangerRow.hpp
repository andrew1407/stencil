#pragma once
// A destructive menu row wears its colour on BOTH halves, as the browser's
// .project-menu-item.is-danger does. Qt has no per-action colour, and a QProxyStyle is no
// way in: once any rule targets QMenu::item — the app's paddings do — QStyleSheetStyle
// draws the item itself and never calls the base style. So the row is flagged as the
// menu's DEFAULT item and styled through `QMenu::item:default`; not `:checked`, which
// would also make the action checkable.
#include <QAction>
#include <QColor>
#include <QMenu>
#include <QString>

namespace stencil::support {

  // Mark `action` as `menu`'s destructive row. Call AFTER any helper that assigns the
  // menu's stylesheet (gui::compactIconMenu replaces it wholesale) — this appends to it.
  inline void markDangerRow(QMenu& menu, QAction* action, const QColor& danger) {
    if (!action) return;
    menu.setDefaultAction(action);
    menu.setStyleSheet(
        menu.styleSheet()
        + QStringLiteral(
              // font-weight pinned: a "default" item is bolded by some styles, and this
              // one is red to be read, not to shout.
              "\nQMenu::item:default{color:%1;font-weight:normal;}"
              // …and under the cursor it fills, white on danger, exactly as the browser's
              // .project-menu-item.is-danger:hover does.
              "\nQMenu::item:default:selected{background:%1;color:#ffffff;}")
              .arg(danger.name()));
  }

}  // namespace stencil::support

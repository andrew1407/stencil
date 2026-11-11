#pragma once
// "Make a copy ›" and its three scopes (browser core/project/copy/options.js COPY_SCOPE_LABELS and
// COPY_SCOPE_ICONS): the projects row menu, the canvas context menu and the toolbar's Image button
// all build their rows here, so the labels, icons and order cannot drift apart.
#include "iconSet.hpp"
#include "menuReveal.hpp"

#include <QAction>
#include <QColor>
#include <QMenu>
#include <QString>

#include <functional>

namespace stencil::support {

  enum class CopyScope { IMAGE, LAYOUT, PROJECT };

  struct CopyScopeRow {
    CopyScope scope;
    const char* icon;
    const char* label;
  };
  inline constexpr CopyScopeRow COPY_SCOPE_ROWS[] = {
      {CopyScope::IMAGE, "image", "Image only"},
      {CopyScope::LAYOUT, "layers", "Image and layout"},
      {CopyScope::PROJECT, "folder", "Whole project"},
  };
  inline constexpr const char* COPY_MENU_ICON = "duplicate";
  inline constexpr const char* COPY_MENU_LABEL = "Make a copy";

  inline const char* copyScopeLabel(CopyScope scope) {
    for (const CopyScopeRow& row : COPY_SCOPE_ROWS)
      if (row.scope == scope) return row.label;
    return "";
  }

  // The three scope rows, appended to `menu`.
  inline void addCopyScopes(QMenu& menu, const QColor& ink, int iconSize,
                            const std::function<void(CopyScope)>& pick) {
    for (const CopyScopeRow& row : COPY_SCOPE_ROWS) {
      const CopyScope scope = row.scope;
      QAction* a = menu.addAction(gui::themedIcon(QString::fromLatin1(row.icon), ink, iconSize),
                                  QString::fromUtf8(row.label));
      QObject::connect(a, &QAction::triggered, &menu, [pick, scope] { pick(scope); });
    }
  }

  // A "Make a copy" flyout under `parent`, growing its dust from its own row on `dustMs`; the
  // projects row names it "Copy project" (browser projects/row/copyItem.js).
  inline QMenu* addCopyProjectMenu(QMenu& parent, const QColor& ink, int iconSize, int dustMs,
                                   const std::function<void(CopyScope)>& pick,
                                   const char* label = COPY_MENU_LABEL) {
    auto* sub = new QMenu(QString::fromUtf8(label), &parent);
    QAction* opener = parent.addMenu(sub);
    opener->setIcon(gui::themedIcon(QString::fromLatin1(COPY_MENU_ICON), ink, iconSize));
    revealSubmenu(*sub, parent, *opener, dustMs);
    addCopyScopes(*sub, ink, iconSize, pick);
    return sub;
  }

}  // namespace stencil::support

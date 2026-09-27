#pragma once
#include <QHash>
#include <QString>
#include <QStringList>

class QAction;

namespace stencil::gui {

  // The window's shortcut table (browser hotkeysConfig.json + STORAGE_KEYS.hotkeys): what each id
  // is bound to, its default and label, the order the shortcuts dialog lists them, and its action.
  struct HotkeyTable {
    QHash<QString, QString> bound;
    QHash<QString, QString> defaults;
    QHash<QString, QString> labels;
    QStringList order;
    QHash<QString, QAction*> actions;

    void load();
    QString value(const QString& id, const QString& fallback) const { return bound.value(id, fallback); }
  };

}  // namespace stencil::gui

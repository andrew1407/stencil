#include "HotkeyTable.hpp"
#include "fileStore.hpp"

// The shortcut table: the shared hotkeysConfig.json defaults and labels, the user's overrides.

namespace stencil::gui {

  // Defaults + labels from the embedded config, user overrides on top (browser STORAGE_KEYS.hotkeys merge).
  void HotkeyTable::load() {
    QFile hk(":/config/hotkeysConfig.json");
    if (hk.open(QIODevice::ReadOnly)) {
      for (const auto& v : QJsonDocument::fromJson(hk.readAll()).array()) {
        const QJsonObject o = v.toObject();
        const QString id = o.value("id").toString();
        const QString def = o.value("default").toString();
        defaults.insert(id, def);
        labels.insert(id, o.value("label").toString());
        bound.insert(id, def);
        order.append(id);
      }
    }
    // Alt+Shift+arrow chords fire from keyPressEvent (no QAction): defaults + labels only, so the shortcuts dialog lists them.
    struct ChordDef { const char* id; const char* seq; const char* label; };
    static const ChordDef LINE_TRANSFORM_CHORDS[] = {
        {"flipLineHorizontal", "Alt+Shift+Up", "Flip Selected Line Horizontal"},
        {"flipLineVertical", "Alt+Shift+Down", "Flip Selected Line Vertical"},
        {"rotateLineCW90", "Alt+Shift+Right", "Rotate Selected Line +90°"},
        {"rotateLineCCW90", "Alt+Shift+Left", "Rotate Selected Line −90°"},
    };
    for (const auto& c : LINE_TRANSFORM_CHORDS) {
      defaults.insert(c.id, c.seq);
      labels.insert(c.id, c.label);
      bound.insert(c.id, c.seq);
      order.append(c.id);
    }
    const auto overrides = fileStore::loadHotkeys();
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
      bound.insert(it.key(), it.value());
  }
}  // namespace stencil::gui

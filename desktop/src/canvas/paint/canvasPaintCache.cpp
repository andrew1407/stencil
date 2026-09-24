#include "canvasPaintCache.hpp"

#include "cssColor.hpp"
#include "../../support/skinPrefs.hpp"

#include <QHash>
#include <unordered_map>

namespace stencil::gui {

  const Palette& paintPalette(bool dark, const QString& accentKey, const QColor& selGlow,
                              const QColor& hoverRing) {
    static QHash<QString, Palette> cache;
    const QString key = QStringLiteral("%1|%2|%3|%4|%5")
                            .arg(accentKey).arg(int(dark))
                            .arg(selGlow.rgba()).arg(hoverRing.rgba())
                            .arg(stencil::support::skinGeneration());   // themePalette follows the skin
    auto it = cache.find(key);
    if (it == cache.end()) {
      if (cache.size() > 64) cache.clear();   // a custom accent/highlight mints new keys
      Palette p = themePalette(dark, accentKey);
      // The user's own highlight-style choices (Settings), not the theme's fixed
      // defaults — setHighlightColors() is what moves these off DEFAULT_VISUALS.
      p.selGlow = selGlow;
      p.hoverRing = hoverRing;
      it = cache.insert(key, p);
    }
    return *it;
  }

  const QColor& paintColor(const std::string& css) {
    static std::unordered_map<std::string, QColor> cache;
    auto it = cache.find(css);
    if (it == cache.end()) {
      if (cache.size() > 256) cache.clear();
      it = cache.emplace(css, cssColor(css)).first;
    }
    return it->second;
  }

}  // namespace stencil::gui

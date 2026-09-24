#include "look.hpp"

#include "icons.hpp"
#include "rules.hpp"
#include "skinPrefs.hpp"
#include "stylesheet.hpp"
#include "../tip/SnappyTooltipStyle.hpp"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>

namespace stencil::support {

  namespace {
    // The first family the host has of the skin's list; the last entry is the generic fallback.
    QFont skinFont() {
      const WebcoreConfig& c = webcoreConfig();
      const QStringList have = QFontDatabase::families();
      QString family = c.fontFamilies.isEmpty() ? QString() : c.fontFamilies.last();
      for (const QString& f : c.fontFamilies)
        if (have.contains(f, Qt::CaseInsensitive)) { family = f; break; }
      QFont font(family);
      font.setPixelSize(c.fontPx);   // the QSS sizes every control to the same number
      font.setStyleStrategy(QFont::NoAntialias);
      return font;
    }

    QFont& priorFont() { static QFont f; return f; }
    bool& fontHeld() { static bool held = false; return held; }
  }  // namespace

  void applyWebcoreLook(bool on) {
    if (on) {
      setSkinPalette(&webcorePalette);
      setSkinIcon(&pixelIconSvg);
      installAppStyle("Windows");
      if (!fontHeld()) { priorFont() = QApplication::font(); fontHeld() = true; }
      QApplication::setFont(skinFont());
      return;
    }
    setSkinPalette(nullptr);
    setSkinIcon(nullptr);
    installAppStyle("Fusion");
    if (fontHeld()) { QApplication::setFont(priorFont()); fontHeld() = false; }
  }

}  // namespace stencil::support

#pragma once
// The session skin every restyle asks (browser <html data-skin>): which one is on, the theme it
// forces, and the two hooks a skin hangs its palette and its icons on. Header-only like
// motionPrefs.hpp; nothing here reaches disk.
#include <QColor>
#include <QString>
#include <optional>

namespace stencil::gui { struct Palette; }

namespace stencil::support {

  enum class Skin { DEFAULT, WEBCORE };

  // The skin's raised box (browser css/webcore/tokens.css --wc-raised): its face and ink, the
  // title strip's two stops (--wc-title runs a to b across it), the outer band pair, then the
  // pair one pixel in. Pushed with the palette; a painter that wants the box reads it here.
  struct SkinBevel { QColor face, ink, title, titleB, hilight, dark, light, shadow; };
  using PaletteFn = stencil::gui::Palette (*)(bool dark);
  using IconFn = QString (*)(const QString& name);

  namespace detail {
    inline Skin& skinState() { static Skin s = Skin::DEFAULT; return s; }
    inline int& skinGenerationState() { static int g = 0; return g; }
    inline std::optional<bool>& forcedDarkState() { static std::optional<bool> f; return f; }
    inline bool& skinDarkState() { static bool d = false; return d; }
    inline SkinBevel& skinBevelState() { static SkinBevel b; return b; }
    inline QColor& skinAccentState() { static QColor c; return c; }
    inline PaletteFn& paletteHookState() { static PaletteFn fn = nullptr; return fn; }
    inline IconFn& iconHookState() { static IconFn fn = nullptr; return fn; }
  }  // namespace detail

  inline Skin skin() { return detail::skinState(); }
  inline bool isWebcore() { return skin() == Skin::WEBCORE; }
  // Bumped on every change, so a cache keyed on it forgets the other skin's art.
  inline int skinGeneration() { return detail::skinGenerationState(); }
  inline void setSkin(Skin s) {
    if (detail::skinState() != s) ++detail::skinGenerationState();
    detail::skinState() = s;
  }

  // The theme the skin's art is drawn for; pushed by MainWindow::applyTheme. It bumps the
  // generation, so the icon cache forgets the other face (browser: setPixelIcons(on, dark)).
  inline bool skinDark() { return detail::skinDarkState(); }
  inline void setSkinDark(bool dark) {
    if (detail::skinDarkState() != dark) ++detail::skinGenerationState();
    detail::skinDarkState() = dark;
  }

  // The theme painted instead of the stored mode, until the user chooses one.
  inline std::optional<bool> forcedDark() { return detail::forcedDarkState(); }
  inline void setForcedDark(bool dark) { detail::forcedDarkState() = dark; }
  inline void clearForcedDark() { detail::forcedDarkState().reset(); }

  inline SkinBevel skinBevel() { return detail::skinBevelState(); }
  inline void setSkinBevel(const SkinBevel& b) { detail::skinBevelState() = b; }

  // The preset the user chose, which the skin paints its pointer frame and its key terms with
  // (browser: toggle.js writes it to --wc-focus). Invalid until a sheet has been built.
  inline QColor skinAccent() { return detail::skinAccentState(); }
  inline void setSkinAccent(const QColor& c) { detail::skinAccentState() = c; }

  inline PaletteFn skinPalette() { return detail::paletteHookState(); }
  inline void setSkinPalette(PaletteFn fn) { detail::paletteHookState() = fn; }
  // A complete <svg> document for a name, or empty to fall through to the line-art.
  inline IconFn skinIcon() { return detail::iconHookState(); }
  inline void setSkinIcon(IconFn fn) { detail::iconHookState() = fn; }

}  // namespace stencil::support

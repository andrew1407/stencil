#pragma once
// The session skin every restyle asks (browser <html data-skin>): which one is on, the theme it
// forces, and the colours its sheet pushes for the hand-painted chrome. The look itself is the
// strategy skin/SkinLookStrategy.hpp installs. Header-only like motionPrefs.hpp; nothing here reaches disk.
#include <QBrush>
#include <QColor>
#include <optional>

namespace stencil::support {

  enum class Skin { DEFAULT, WEBCORE, AQUA };

  // The skin's raised box (browser css/webcore/tokens.css --wc-raised): face and ink, the title strip's two
  // stops (--wc-title runs a to b), the outer band pair, then the pair one pixel in; pushed with the palette.
  struct SkinBevel { QColor face, ink, title, titleB, hilight, dark, light, shadow; };
  // A drop target's ring and halo where the skin draws its own (browser css/aqua/canvas.css: the
  // gel's edge and its focus glow); invalid under a skin that leaves it to the accent.
  struct SkinDropGlow { QColor ring, halo; };
  // The skin's glass capsule and its pressed gel (browser css/aqua/tokens.css --aq-glass, --aq-gel and
  // their rings), with each fill's stops ready for a painter; invalid under a skin without one.
  struct SkinGlass {
    QColor top, bottom, edge, ink, gelHi, gelTop, gelBottom, gelEdge, onGel;
    QGradientStops glassStops, gelStops;
  };

  namespace detail {
    inline Skin& skinState() { static Skin s = Skin::DEFAULT; return s; }
    inline int& skinGenerationState() { static int g = 0; return g; }
    inline std::optional<bool>& forcedDarkState() { static std::optional<bool> f; return f; }
    inline bool& skinDarkState() { static bool d = false; return d; }
    inline SkinBevel& skinBevelState() { static SkinBevel b; return b; }
    inline QColor& skinAccentState() { static QColor c; return c; }
    inline SkinDropGlow& dropGlowState() { static SkinDropGlow g; return g; }
    inline SkinGlass& glassState() { static SkinGlass g; return g; }
  }  // namespace detail

  inline Skin skin() { return detail::skinState(); }
  // Set once the process's first window has re-worn the skin stored at the last quit.
  inline bool& skinRestored() { static bool done = false; return done; }
  inline bool isWebcore() { return skin() == Skin::WEBCORE; }
  inline bool isAqua() { return skin() == Skin::AQUA; }
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

  inline SkinDropGlow skinDropGlow() { return detail::dropGlowState(); }
  inline void setSkinDropGlow(const SkinDropGlow& g) { detail::dropGlowState() = g; }

  inline const SkinGlass& skinGlass() { return detail::glassState(); }
  inline void setSkinGlass(const SkinGlass& g) { detail::glassState() = g; }

}  // namespace stencil::support

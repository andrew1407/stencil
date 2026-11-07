#pragma once
// A session skin as the restyle sees it (browser: the skin's tokens.css, its icons and its toggle): its
// palette, the base tokens it repaints, its icons, its sheet, its mark, its particle stops and its traits.
// One look is installed at a time; the default answers for the theme's own.
#include "SkinTraits.hpp"
#include "theme.hpp"

#include <QColor>
#include <QImage>
#include <QString>
#include <optional>
#include <utility>

namespace stencil::support {

  // Where a type has an empty value (an invalid colour, an empty string, a null image) it answers "the theme's own";
  // where it has none, the answer is std::optional.
  class SkinLookStrategy {
   public:
    // The mark as the skin draws it, `ink` the colour iconFromMarkup paints it in.
    struct LogoArt { QString svg; QColor ink; };
    // The panel grip's ink at rest and lit.
    struct GripInk { QColor rest; QColor lit; };

    virtual ~SkinLookStrategy() = default;
    virtual const SkinTraits& traits() const = 0;
    // The hand-painted chrome's palette; nullopt where the theme's own stands.
    virtual std::optional<stencil::gui::Palette> palette(bool dark) const = 0;
    // A base theme token as the skin repaints it (browser: `--bg-keywords: var(--aq-window)`); invalid where it leaves it alone.
    virtual QColor token(const char* css, bool dark) const = 0;
    // A complete <svg> document for a glyph, or empty to fall through to the line-art.
    virtual QString iconSvg(const QString& name) const = 0;
    // The same glyph as a raster, `scale` device pixels a 1/16 of its side; null for an unknown name.
    virtual QImage iconImage(const QString& name, bool dark, int scale) const = 0;
    // The app sheet in this look; empty where the theme's own (gui::buildStylesheet) stands.
    virtual QString stylesheet(bool dark, const QString& accentKey) const = 0;
    // The skin's own mark; nullopt where the theme's painted S-mark stands.
    virtual std::optional<LogoArt> logoArt(const QColor& accent, bool dark) const = 0;
    // The particle cloud's two stops (browser --dust-accent / --dust-accent-2).
    virtual std::pair<QColor, QColor> particleStops(const QColor& accent, const stencil::gui::Palette& pal, bool dark) const = 0;
    virtual GripInk gripInk(const stencil::gui::Palette& pal, bool dark) const = 0;
  };

  // The theme's own answers: no skin palette, tokens, icons, sheet or mark; the accent and the key ink.
  class DefaultLookStrategy : public SkinLookStrategy {
   public:
    const SkinTraits& traits() const override { return DEFAULT_TRAITS; }
    std::optional<stencil::gui::Palette> palette(bool) const override { return std::nullopt; }
    QColor token(const char*, bool) const override { return QColor(); }
    QString iconSvg(const QString&) const override { return QString(); }
    QImage iconImage(const QString&, bool, int) const override { return QImage(); }
    QString stylesheet(bool, const QString&) const override { return QString(); }
    std::optional<LogoArt> logoArt(const QColor&, bool) const override { return std::nullopt; }
    std::pair<QColor, QColor> particleStops(const QColor&, const stencil::gui::Palette& pal, bool) const override {
      return {pal.accent, pal.textKey};
    }
    GripInk gripInk(const stencil::gui::Palette& pal, bool) const override { return {pal.borderMain, pal.accent}; }
  };

  namespace detail {
    inline const SkinLookStrategy*& skinLookState() { static const SkinLookStrategy* look = nullptr; return look; }
  }  // namespace detail

  inline const SkinLookStrategy& defaultLook() { static const DefaultLookStrategy look; return look; }
  inline const SkinLookStrategy& skinLook() { return detail::skinLookState() ? *detail::skinLookState() : defaultLook(); }
  // Null takes the default back.
  inline void installSkinLook(const SkinLookStrategy* look) { detail::skinLookState() = look; }
  inline const SkinTraits& skinTraits() { return skinLook().traits(); }

}  // namespace stencil::support

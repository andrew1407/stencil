#include "look.hpp"

#include "icons.hpp"
#include "sheet.hpp"
#include "table.hpp"
#include "../skin/SkinLookStrategy.hpp"
#include "../skin/skinAppliers.hpp"
#include "../skin/skinFont.hpp"
#include "../skinPrefs.hpp"

namespace stencil::support {

  namespace {
    class AquaLookStrategy final : public DefaultLookStrategy {
     public:
      const SkinTraits& traits() const override { return AQUA_TRAITS; }
      std::optional<stencil::gui::Palette> palette(bool dark) const override { return aquaPalette(dark); }
      QColor token(const char* css, bool dark) const override { return aquaToken(css, dark); }
      QString iconSvg(const QString& name) const override { return aquaIconSvg(name); }
      QImage iconImage(const QString& name, bool dark, int scale) const override { return aquaIconImage(name, dark, scale); }
      QString stylesheet(bool dark, const QString& accentKey) const override { return buildAquaStylesheet(dark, accentKey); }
      std::optional<LogoArt> logoArt(const QColor& accent, bool dark) const override { return LogoArt{aquaLogoArt(accent, dark), QColor()}; }
    };
    const SkinLookStrategy& aquaLook() {
      static const AquaLookStrategy look;
      return look;
    }
  }  // namespace

  void applyAquaLook(bool isOn) {
    if (isOn) {
      installSkinLook(&aquaLook());
      const AquaConfig& c = aquaConfig();
      holdSkinFont(skinFont(c.fontFamilies, c.fontPx, /*antialiased=*/true));
      return;
    }
    installSkinLook(nullptr);
    setSkinDropGlow({});   // the sheet build set the glow and the glass; off, they go with the look
    setSkinGlass({});
    releaseSkinFont();
  }

}  // namespace stencil::support

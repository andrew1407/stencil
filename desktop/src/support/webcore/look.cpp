#include "look.hpp"

#include "icons.hpp"
#include "rules.hpp"
#include "stylesheet.hpp"
#include "../skin/SkinLookStrategy.hpp"
#include "../skin/skinAppliers.hpp"
#include "../skin/skinFont.hpp"
#include "../tip/SnappyTooltipStyle.hpp"

namespace stencil::support {

  namespace {
    class WebcoreLookStrategy final : public SkinLookStrategy {
     public:
      const SkinTraits& traits() const override { return WEBCORE_TRAITS; }
      std::optional<stencil::gui::Palette> palette(bool dark) const override { return webcorePalette(dark); }
      QColor token(const char*, bool) const override { return QColor(); }   // its sheet restyles each token's users itself
      QString iconSvg(const QString& name) const override { return pixelIconSvg(name); }
      QImage iconImage(const QString& name, bool dark, int scale) const override { return pixelIconImage(name, dark, scale); }
      QString stylesheet(bool dark, const QString& accentKey) const override { return buildWebcoreStylesheet(dark, accentKey); }
      // The pixel S-mark, its ring in the accent.
      std::optional<LogoArt> logoArt(const QColor& accent, bool) const override {
        static const QColor DEFAULT_RING(stencil::gui::DEFAULT_ACCENT_HEX);
        const QColor ring = accent.isValid() ? accent : DEFAULT_RING;
        return LogoArt{pixelLogoSvg(ring), ring};
      }
      // The skin repoints the palette's accent at its navy; the particles keep the chosen colour and a second
      // stop mixed towards the face's edge (browser webcore/tokens.css --dust-accent / --dust-accent-2).
      std::pair<QColor, QColor> particleStops(const QColor& accent, const stencil::gui::Palette&, bool dark) const override {
        const QColor edge = dark ? QColor(Qt::white) : QColor(Qt::black);
        const double k = dark ? 0.78 : 0.86;
        return {accent, QColor::fromRgbF(accent.redF() * k + edge.redF() * (1 - k), accent.greenF() * k + edge.greenF() * (1 - k),
                                         accent.blueF() * k + edge.blueF() * (1 - k))};
      }
      // The grip is the ink on the dark face, the shadow on the light one; lit, the chosen accent (--wc-focus).
      GripInk gripInk(const stencil::gui::Palette& pal, bool dark) const override { return {dark ? pal.textMain : pal.borderMain, pal.textKey}; }
    };
  }  // namespace

  const SkinLookStrategy& webcoreLook() {
    static const WebcoreLookStrategy look;
    return look;
  }

  void applyWebcoreLook(bool isOn) {
    if (isOn) {
      installSkinLook(&webcoreLook());
      installAppStyle("Windows");
      const WebcoreConfig& c = webcoreConfig();
      holdSkinFont(skinFont(c.fontFamilies, c.fontPx, /*antialiased=*/false));
      return;
    }
    installSkinLook(nullptr);
    installAppStyle("Fusion");
    releaseSkinFont();
  }

}  // namespace stencil::support

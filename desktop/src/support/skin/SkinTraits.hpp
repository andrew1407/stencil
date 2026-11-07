#pragma once
// What a skin answers with a yes or a no (browser css/skin/still.css and each skin's chrome.css): the
// motion it stills, the corners it squares, the type it keeps as written. The theme's own answers are the
// defaults; each skin states only what it changes, by name.
namespace stencil::support {

  struct SkinTraits {
    bool hasRowHoverStep = true;       // a hovered row nudges 2px (HoverSlide, RowHoverSlide)
    bool hasHoverLift = true;          // a hovered control lifts 1px under its glow
    bool hasShimmer = true;            // a band of light sweeps a changed control
    bool hasIconMotion = true;         // the line-art glyphs' hover and click motion
    bool hasKeycapShake = true;        // a tip's keycaps and a menu row's chips shake once
    bool isLogoStill = false;          // the hovered mark grows and glows instead of pulsing
    bool hasLogoGlow = true;           // …with an edge glow while still (aqua magnifies, never glows)
    bool hasIdleCardMotion = true;     // the empty canvas's card blends, lifts, settles and sweeps
    bool hasToastShine = true;         // the aura round a notice
    bool hasChatTail = true;           // a chat bubble's tail
    bool hasSquareCorners = false;     // nothing is rounded
    bool hasShoutingCaptions = true;   // a section heading in capitals
    bool isInfoMono = true;            // the info dialog's terms in a monospace face
    bool isLensAllowed = true;         // the theme lens opens (never under a skin, as in the browser)
    bool isInfoInkMuted = true;        // the size readout and the title in the muted grey, else the window's ink
    bool hasGlassToggles = false;      // a lit toggle is glass carrying the ink, not a fill the ink sits on
    bool hasMenuCheckIcons = true;     // a checkable menu row draws its box as an icon (webcore's row is bare)
    bool hasPixelSwatch = false;       // the accent swatch is a square pixel chip with the skin's own ✓ art
    bool hasAlertSheet = false;        // a notice wears the skin's palette as a sheet (face, ink, frame)
    bool isDisabledIconGreyed = false; // a disabled baked icon is greyed and faded, as the browser's filter
    double logoHoverScale = 1.08;      // the mark's grow while hovered and still
    int fieldTextMarginPx = 0;         // a spin box's left text margin
    int accentMenuInsetPx = 8;         // the accent popover's padding (browser .logo-accent-menu)
    char16_t bullet = u'●';            // the connection lamp
  };

  inline constexpr SkinTraits DEFAULT_TRAITS{};

  constexpr SkinTraits webcoreTraits() {
    SkinTraits t;
    t.hasIconMotion = false;
    t.hasKeycapShake = false;
    t.isLogoStill = true;
    t.hasToastShine = false;
    t.hasChatTail = false;
    t.hasSquareCorners = true;
    t.hasShoutingCaptions = false;
    t.isInfoMono = false;
    t.isLensAllowed = false;
    t.isInfoInkMuted = false;
    t.hasMenuCheckIcons = false;
    t.hasPixelSwatch = true;
    t.fieldTextMarginPx = 4;
    t.accentMenuInsetPx = 3;
    t.bullet = u'■';
    return t;
  }

  constexpr SkinTraits aquaTraits() {
    SkinTraits t;
    t.hasRowHoverStep = false;
    t.hasHoverLift = false;
    t.hasShimmer = false;
    t.hasIconMotion = false;
    t.hasKeycapShake = false;
    t.isLogoStill = true;
    t.hasLogoGlow = false;
    t.hasIdleCardMotion = false;
    t.hasShoutingCaptions = false;
    t.isInfoMono = false;
    t.isLensAllowed = false;
    t.isInfoInkMuted = false;
    t.hasGlassToggles = true;
    t.hasAlertSheet = true;
    t.isDisabledIconGreyed = true;
    t.logoHoverScale = 1.15;
    t.fieldTextMarginPx = 11;
    return t;
  }

  inline constexpr SkinTraits WEBCORE_TRAITS = webcoreTraits();
  inline constexpr SkinTraits AQUA_TRAITS = aquaTraits();

}  // namespace stencil::support

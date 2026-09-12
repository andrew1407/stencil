#pragma once
// The card paints its `image` glyph by hand (canvas/CanvasWidget.cpp) rather than through
// support/iconSet — that would drag Qt6::Svg into every headless target compiling the
// canvas — so the app-wide hover watcher, which knows QAbstractButtons and the QIcons they
// carry (support/iconMotion.hpp), cannot reach it and the glyph sat still on hover while
// every other icon in the app moved.
//
// These mirror the canon's own entry for that glyph (browser js/config/iconMotion.json,
// "image", mode "settle"): the ridge draws itself on, and the little sun drops in a beat
// later. Their own header so the icon-motion test can read them without the whole canvas
// widget; tests/iconMotion.headless.cpp pins every one against the JSON.
namespace stencil::gui {

  inline constexpr double IDLE_GLYPH_RIDGE_MS = 340;       // parts[ic-ridge].durationMs
  inline constexpr double IDLE_GLYPH_RIDGE_LEN = 23;       // …dashArray — the polyline's length
  inline constexpr double IDLE_GLYPH_ORB_MS = 300;         // parts[ic-orb].durationMs
  inline constexpr double IDLE_GLYPH_ORB_DELAY_MS = 90;     // …delayMs
  inline constexpr double IDLE_GLYPH_ORB_DROP = -1.6;      // …keyframes[0].translate y
  inline constexpr double IDLE_GLYPH_ORB_OVERSHOOT = 0.3;  // …keyframes[1] (at 70%)
  // The whole play — the later of the two parts' ends (90 + 300).
  inline constexpr double IDLE_GLYPH_PLAY_MS = 390;

}  // namespace stencil::gui

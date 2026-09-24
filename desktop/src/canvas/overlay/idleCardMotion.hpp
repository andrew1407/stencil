#pragma once
// The card paints its `image` glyph by hand (canvas/CanvasWidget.cpp) rather than through
// support/iconSet - that would drag Qt6::Svg into every headless target compiling the canvas -
// so the app-wide hover watcher (support/iconMotion.hpp) cannot reach it.
// These mirror the canon's entry for that glyph (browser js/config/iconMotion.json, "image",
// mode "settle"); tests/iconMotion.headless.cpp pins every one against the JSON.
namespace stencil::gui {

  inline constexpr double IDLE_GLYPH_RIDGE_MS = 340;       // parts[ic-ridge].durationMs
  inline constexpr double IDLE_GLYPH_RIDGE_LEN = 23;       // …dashArray — the polyline's length
  inline constexpr double IDLE_GLYPH_ORB_MS = 300;         // parts[ic-orb].durationMs
  inline constexpr double IDLE_GLYPH_ORB_DELAY_MS = 90;     // …delayMs
  inline constexpr double IDLE_GLYPH_ORB_DROP = -1.6;      // …keyframes[0].translate y
  inline constexpr double IDLE_GLYPH_ORB_OVERSHOOT = 0.3;  // …keyframes[1] (at 70%)
  // The whole play — the later of the two parts' ends (90 + 300).
  inline constexpr double IDLE_GLYPH_PLAY_MS = 390;
  // The card's own arrival when a picture leaves (browser @keyframes idleCardArrive).
  inline constexpr int IDLE_CARD_ARRIVE_MS = 340;
  inline constexpr double IDLE_CARD_ARRIVE_FROM = 0.86;

}  // namespace stencil::gui

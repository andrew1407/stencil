#pragma once
// The accent every unset or unrecognized value falls back to: the first preset in
// browser/js/config/accents.json. Include-free so a header may take it as a member default.
namespace stencil::gui {

  inline constexpr const char* DEFAULT_ACCENT_KEY = "violet";
  inline constexpr const char* DEFAULT_ACCENT_HEX = "#7c3aed";

}  // namespace stencil::gui

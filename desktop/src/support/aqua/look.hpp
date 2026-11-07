#pragma once
// The application half of the aqua skin, MainWindow-free: the Lucida face, the look the restyle
// reads (support/skin/SkinLookStrategy.hpp) and the session Blue's set-aside accent. The style stays Fusion;
// the overlay does the gel.
#include <QString>
#include <optional>

namespace stencil::support {

  // The preset the skin opens in for the session (browser ui/aqua/toggle.js AQUA_ACCENT).
  inline constexpr char AQUA_ACCENT[] = "blue";
  // The accent the session Blue set aside, the one the settings file holds; empty once the user
  // picks an accent of their own while the skin is on. Process-wide, as the skin is.
  inline std::optional<QString>& setAsideAccent() { static std::optional<QString> own; return own; }

}  // namespace stencil::support

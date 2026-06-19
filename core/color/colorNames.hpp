#pragma once
#include <optional>
#include <string>

// CSS colour parsing for the headless pipeline. The desktop app leans on QColor and
// the browser on the canvas/CSS engine to turn 'red' or '#abc' into pixels; the
// standalone core (and the Zig CLI that drives it) needs its own resolver. Accepts
// CSS extended colour keywords, #rgb / #rgba / #rrggbb / #rrggbbaa hex, and the
// keyword 'transparent'. Complements color.hpp (which is hex-only). Pure, STL-only.
namespace stencil::core {

  struct Rgba {
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 255;
  };

  // Parse a colour spec to RGBA (0–255). Case-insensitive. nullopt if unrecognized.
  // 'transparent' resolves to {0,0,0,0}.
  std::optional<Rgba> parseColor(const std::string& spec);

}

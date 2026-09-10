#pragma once
#include <cstddef>
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

  // Enumerate the keyword table alphabetically, so an adapter can drift-check its
  // own copy (browser/js/config/colorNames.json) in BOTH directions. 'transparent'
  // is not in here: parseColor handles it on its own and it has no hex.
  // colorNameAt returns nullptr out of range and writes 0xRRGGBB to *rgb.
  std::size_t colorNameCount();
  const char* colorNameAt(std::size_t index, unsigned* rgb);

}

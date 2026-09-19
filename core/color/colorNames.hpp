#pragma once
#include <cstddef>
#include <optional>
#include <string>

// CSS colour resolver for the headless pipeline (the GUIs have QColor / the canvas):
// extended keywords, #rgb / #rgba / #rrggbb / #rrggbbaa and 'transparent'. Complements
// the hex-only color.hpp.
namespace stencil::core {

  struct Rgba {
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 255;
  };

  // Case-insensitive; 'transparent' is {0,0,0,0}; nullopt if unrecognized.
  std::optional<Rgba> parseColor(const std::string& spec);

  // Alphabetical, so an adapter can drift-check its copy of browser/js/config/colorNames.json
  // in BOTH directions. 'transparent' is not in here. nullptr out of range, 0xRRGGBB in *rgb.
  std::size_t colorNameCount();
  const char* colorNameAt(std::size_t index, unsigned* rgb);

}

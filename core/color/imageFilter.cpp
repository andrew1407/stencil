#include "imageFilter.hpp"
#include <algorithm>
#include <cmath>

namespace stencil::core {

  FilterMode filterModeFromString(const std::string& mode) {
    if (mode.empty() || mode == "none") return FilterMode::None;
    if (mode == "bw") return FilterMode::Bw;
    if (mode == "sepia") return FilterMode::Sepia;
    return FilterMode::Custom;  // renderer.js: any other value is the custom tint
  }

  // Per-pixel transform. The exact arithmetic mirrors what the desktop canvas
  // previously computed inline (Rec. 709 luma via truncation, the CSS sepia
  // matrix clamped high, and the duotone tint via std::lround) so the desktop's
  // filtered output is byte-for-byte unchanged after the extraction.
  Rgb8 filterPixel(FilterMode mode, int r, int g, int b, int tintR, int tintG,
                   int tintB) {
    switch (mode) {
      case FilterMode::None:
        return {r, g, b};

      case FilterMode::Bw: {
        // Rec. 709 luma (renderer.js grayscale(100%)).
        const int l =
            static_cast<int>(0.2126 * r + 0.7152 * g + 0.0722 * b);
        return {l, l, l};
      }

      case FilterMode::Sepia: {
        // CSS sepia(100%) matrix, clamped to [0, 255].
        const int sr =
            std::min(255, static_cast<int>(0.393 * r + 0.769 * g + 0.189 * b));
        const int sg =
            std::min(255, static_cast<int>(0.349 * r + 0.686 * g + 0.168 * b));
        const int sb =
            std::min(255, static_cast<int>(0.272 * r + 0.534 * g + 0.131 * b));
        return {sr, sg, sb};
      }

      case FilterMode::Custom: {
        // Grayscale -> duotone tint (renderer.js #applyTintFilter): dark pixels
        // -> the tint color, light pixels -> white.
        const int l =
            static_cast<int>(0.2126 * r + 0.7152 * g + 0.0722 * b);
        const double t = l / 255.0;  // 0 dark->color, 1 light->white
        return {
            static_cast<int>(std::lround(tintR + (255 - tintR) * t)),
            static_cast<int>(std::lround(tintG + (255 - tintG) * t)),
            static_cast<int>(std::lround(tintB + (255 - tintB) * t)),
        };
      }
    }
    return {r, g, b};  // unreachable; keeps the compiler happy
  }

  void applyFilterRGBA(FilterMode mode, std::uint8_t* data, std::size_t pixelCount,
                       int tintR, int tintG, int tintB) {
    if (mode == FilterMode::None || data == nullptr) return;
    for (std::size_t i = 0; i < pixelCount; ++i) {
      std::uint8_t* px = data + i * 4;
      const Rgb8 o = filterPixel(mode, px[0], px[1], px[2], tintR, tintG, tintB);
      px[0] = static_cast<std::uint8_t>(o.r);
      px[1] = static_cast<std::uint8_t>(o.g);
      px[2] = static_cast<std::uint8_t>(o.b);
      // px[3] (alpha) is left unchanged.
    }
  }

}

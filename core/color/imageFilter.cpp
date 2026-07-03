#include "imageFilter.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace stencil::core {

  FilterMode filterModeFromString(const std::string& mode) {
    if (mode.empty() || mode == "none") return FilterMode::None;
    if (mode == "bw") return FilterMode::Bw;
    if (mode == "sepia") return FilterMode::Sepia;
    if (mode == "invert") return FilterMode::Invert;
    if (mode == "contour") return FilterMode::Contour;
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

      case FilterMode::Invert:
        // CSS invert(100%): flip every channel.
        return {255 - r, 255 - g, 255 - b};

      case FilterMode::Contour:
        // A no-op here — contour needs dimensions, use applyContourRGBA.
        return {r, g, b};
    }
    return {r, g, b};  // unreachable; keeps the compiler happy
  }

  void applyFilterRGBA(FilterMode mode, std::uint8_t* data, std::size_t pixelCount,
                       int tintR, int tintG, int tintB) {
    // Contour is a no-op here — it needs dimensions, use applyContourRGBA.
    if (mode == FilterMode::None || mode == FilterMode::Contour ||
        data == nullptr)
      return;
    for (std::size_t i = 0; i < pixelCount; ++i) {
      std::uint8_t* px = data + i * 4;
      const Rgb8 o = filterPixel(mode, px[0], px[1], px[2], tintR, tintG, tintB);
      px[0] = static_cast<std::uint8_t>(o.r);
      px[1] = static_cast<std::uint8_t>(o.g);
      px[2] = static_cast<std::uint8_t>(o.b);
      // px[3] (alpha) is left unchanged.
    }
  }

  // The pinned integer-only Sobel (see the header contract): the browser's JS
  // fallback (browser/js/core/contourFilter.js) reimplements exactly this math,
  // so any change here must be mirrored there byte-for-byte.
  void applyContourRGBA(std::uint8_t* data, int width, int height) {
    if (data == nullptr || width <= 0 || height <= 0) return;

    // Luma plane, computed from the ORIGINAL pixels before any output is
    // written: L = (2126*r + 7152*g + 722*b) / 10000 with truncating division.
    // The weights sum to 10000, so L is bounded 0..255 and fits a byte —
    // storing uint8_t quarters the transient allocation; the Sobel sums below
    // still run in int (the operands promote), so output is unchanged.
    const std::size_t count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> luma(count);
    for (std::size_t i = 0; i < count; ++i) {
      const std::uint8_t* px = data + i * 4;
      luma[i] = static_cast<std::uint8_t>(
          (2126 * px[0] + 7152 * px[1] + 722 * px[2]) / 10000);
    }

    // Edge-replicated (clamped) luma lookup.
    const auto l = [&](int x, int y) {
      x = std::clamp(x, 0, width - 1);
      y = std::clamp(y, 0, height - 1);
      return luma[static_cast<std::size_t>(y) * width + x];
    };

    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const int gx = (l(x + 1, y - 1) + 2 * l(x + 1, y) + l(x + 1, y + 1)) -
                       (l(x - 1, y - 1) + 2 * l(x - 1, y) + l(x - 1, y + 1));
        const int gy = (l(x - 1, y + 1) + 2 * l(x, y + 1) + l(x + 1, y + 1)) -
                       (l(x - 1, y - 1) + 2 * l(x, y - 1) + l(x + 1, y - 1));
        const int mag = std::min(255, std::abs(gx) + std::abs(gy));
        std::uint8_t* px =
            data + (static_cast<std::size_t>(y) * width + x) * 4;
        const auto v = static_cast<std::uint8_t>(255 - mag);  // dark edge on white
        px[0] = v;
        px[1] = v;
        px[2] = v;
        // px[3] (alpha) is left unchanged.
      }
    }
  }

}

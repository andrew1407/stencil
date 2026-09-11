#include "imageFilter.hpp"
#include "luma.hpp"
#include "rgba.hpp"  // rgbaOffset
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
        const int l = luma::rec709Truncated(r, g, b);
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
        const int l = luma::rec709Truncated(r, g, b);
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

  namespace {

    // The one filter loop both entry points run: a contiguous run of `count` pixels. The
    // mode switch is hoisted out of it; the per-pixel arithmetic is still filterPixel's.
    void filterRun(FilterMode mode, std::uint8_t* px, std::size_t count, int tintR,
                   int tintG, int tintB) {
      const auto run = [&](auto fn) {
        for (std::size_t i = 0; i < count; ++i, px += 4) {
          const Rgb8 o = fn(px[0], px[1], px[2]);
          px[0] = static_cast<std::uint8_t>(o.r);
          px[1] = static_cast<std::uint8_t>(o.g);
          px[2] = static_cast<std::uint8_t>(o.b);
          // px[3] (alpha) is left unchanged.
        }
      };
      switch (mode) {
        case FilterMode::Bw:
          run([](int r, int g, int b) { return filterPixel(FilterMode::Bw, r, g, b, 0, 0, 0); });
          return;
        case FilterMode::Sepia:
          run([](int r, int g, int b) { return filterPixel(FilterMode::Sepia, r, g, b, 0, 0, 0); });
          return;
        case FilterMode::Invert:
          run([](int r, int g, int b) { return filterPixel(FilterMode::Invert, r, g, b, 0, 0, 0); });
          return;
        case FilterMode::Custom:
          run([&](int r, int g, int b) {
            return filterPixel(FilterMode::Custom, r, g, b, tintR, tintG, tintB);
          });
          return;
        default:
          return;  // None / Contour: nothing to do here
      }
    }

  }  // namespace

  void applyFilterRGBA(FilterMode mode, std::uint8_t* data, std::size_t pixelCount,
                       int tintR, int tintG, int tintB) {
    // Contour is a no-op here — it needs dimensions, use applyContourRGBA.
    if (mode == FilterMode::None || mode == FilterMode::Contour || data == nullptr) return;
    filterRun(mode, data, pixelCount, tintR, tintG, tintB);
  }

  // Rows [y0, y1) of a `width`-wide image. Not expressed as applyFilterRGBA on a
  // sub-pointer only because that entry point counts PIXELS, not rows — both call
  // the same kernel, so the two cannot drift.
  void applyFilterRows(FilterMode mode, std::uint8_t* data, int width, int y0, int y1,
                       int tintR, int tintG, int tintB) {
    if (mode == FilterMode::None || mode == FilterMode::Contour || data == nullptr ||
        width <= 0)
      return;
    if (y0 < 0) y0 = 0;
    if (y1 <= y0) return;
    filterRun(mode, data + rgbaOffset(0, y0, width),
              static_cast<std::size_t>(y1 - y0) * static_cast<std::size_t>(width), tintR,
              tintG, tintB);
  }

  // The pinned integer-only Sobel (see the header contract): the browser's JS
  // fallback (browser/js/core/contourFilter.js) reimplements exactly this math,
  // so any change here must be mirrored there byte-for-byte.
  //
  // Luma plane, computed from the ORIGINAL pixels before any output is written:
  // L = (2126*r + 7152*g + 722*b) / 10000 with truncating division. The weights sum
  // to 10000, so L fits a byte — storing uint8_t quarters the transient allocation;
  // the Sobel sums below still run in int (the operands promote).
  void buildLumaRows(const std::uint8_t* data, int width, int height, int y0, int y1,
                     std::uint8_t* luma) {
    if (data == nullptr || luma == nullptr || width <= 0 || height <= 0) return;
    y0 = std::max(0, y0);
    y1 = std::min(height, y1);
    for (int y = y0; y < y1; ++y) {
      const std::uint8_t* px = data + rgbaOffset(0, y, width);
      std::uint8_t* out = luma + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
      for (int x = 0; x < width; ++x, px += 4)
        out[x] = core::luma::rec709Scaled(px[0], px[1], px[2]);
    }
  }

  void sobelRows(const std::uint8_t* luma, std::uint8_t* data, int width, int height,
                 int y0, int y1) {
    if (luma == nullptr || data == nullptr || width <= 0 || height <= 0) return;
    y0 = std::max(0, y0);
    y1 = std::min(height, y1);
    // Edge-replicated (clamped) luma lookup, for the first/last column only.
    const auto l = [&](int x, int y) {
      return luma[static_cast<std::size_t>(std::clamp(y, 0, height - 1)) *
                      static_cast<std::size_t>(width) +
                  static_cast<std::size_t>(std::clamp(x, 0, width - 1))];
    };
    for (int y = y0; y < y1; ++y) {
      // The y clamp applied once per row: the neighbourhood reads one row OUTSIDE
      // [y0, y1) on each side, which is why the whole luma plane must be built first.
      const std::size_t w = static_cast<std::size_t>(width);
      const std::uint8_t* up = luma + static_cast<std::size_t>(std::max(y - 1, 0)) * w;
      const std::uint8_t* mid = luma + static_cast<std::size_t>(y) * w;
      const std::uint8_t* dn = luma + static_cast<std::size_t>(std::min(y + 1, height - 1)) * w;
      std::uint8_t* px = data + rgbaOffset(0, y, width);
      const auto write = [&](int x, int gx, int gy) {
        const int mag = std::min(255, std::abs(gx) + std::abs(gy));
        const auto v = static_cast<std::uint8_t>(255 - mag);  // dark edge on white
        std::uint8_t* p = px + static_cast<std::size_t>(x) * 4;
        p[0] = v;
        p[1] = v;
        p[2] = v;
        // p[3] (alpha) is left unchanged.
      };
      // Only the first/last column needs the x clamp; out of line it keeps the
      // interior loop branch-free.
      const auto edge = [&](int x) {
        write(x, (l(x + 1, y - 1) + 2 * l(x + 1, y) + l(x + 1, y + 1)) -
                     (l(x - 1, y - 1) + 2 * l(x - 1, y) + l(x - 1, y + 1)),
              (l(x - 1, y + 1) + 2 * l(x, y + 1) + l(x + 1, y + 1)) -
                  (l(x - 1, y - 1) + 2 * l(x, y - 1) + l(x + 1, y - 1)));
      };
      edge(0);
      for (int x = 1; x + 1 < width; ++x) {
        const int a = up[x - 1], b = up[x], c = up[x + 1];
        const int d = mid[x - 1], f = mid[x + 1];
        const int g = dn[x - 1], hh = dn[x], i = dn[x + 1];
        write(x, (c + 2 * f + i) - (a + 2 * d + g), (g + 2 * hh + i) - (a + 2 * b + c));
      }
      if (width > 1) edge(width - 1);
    }
  }

  void applyContourRGBA(std::uint8_t* data, int width, int height,
                        std::vector<std::uint8_t>& scratch) {
    if (data == nullptr || width <= 0 || height <= 0) return;
    scratch.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    buildLumaRows(data, width, height, 0, height, scratch.data());
    sobelRows(scratch.data(), data, width, height, 0, height);
  }

  void applyContourRGBA(std::uint8_t* data, int width, int height) {
    std::vector<std::uint8_t> luma;
    applyContourRGBA(data, width, height, luma);
  }

}

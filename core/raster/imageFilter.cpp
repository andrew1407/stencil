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
    if (mode.empty() || mode == "none") return FilterMode::NONE;
    if (mode == "bw") return FilterMode::BW;
    if (mode == "sepia") return FilterMode::SEPIA;
    if (mode == "invert") return FilterMode::INVERT;
    if (mode == "contour") return FilterMode::CONTOUR;
    return FilterMode::CUSTOM;  // renderer.js: any other value is the custom tint
  }

  Rgb8 filterPixel(FilterMode mode, int r, int g, int b, int tintR, int tintG,
                   int tintB) {
    switch (mode) {
      case FilterMode::NONE:
        return {r, g, b};

      case FilterMode::BW: {
        const int l = luma::rec709Truncated(r, g, b);
        return {l, l, l};
      }

      case FilterMode::SEPIA: {
        const int sr =
            std::min(CHANNEL_MAX, static_cast<int>(0.393 * r + 0.769 * g + 0.189 * b));
        const int sg =
            std::min(CHANNEL_MAX, static_cast<int>(0.349 * r + 0.686 * g + 0.168 * b));
        const int sb =
            std::min(CHANNEL_MAX, static_cast<int>(0.272 * r + 0.534 * g + 0.131 * b));
        return {sr, sg, sb};
      }

      case FilterMode::CUSTOM: {
        const int l = luma::rec709Truncated(r, g, b);
        const double t = static_cast<double>(l) / CHANNEL_MAX;  // 0 dark->color, 1 light->white
        return {
            static_cast<int>(std::lround(tintR + (CHANNEL_MAX - tintR) * t)),
            static_cast<int>(std::lround(tintG + (CHANNEL_MAX - tintG) * t)),
            static_cast<int>(std::lround(tintB + (CHANNEL_MAX - tintB) * t)),
        };
      }

      case FilterMode::INVERT:
        return {CHANNEL_MAX - r, CHANNEL_MAX - g, CHANNEL_MAX - b};

      case FilterMode::CONTOUR:
        return {r, g, b};
    }
    return {r, g, b};  // unreachable; keeps the compiler happy
  }

  namespace {

    // The one loop both entry points run; the mode switch is hoisted out of it.
    void filterRun(FilterMode mode, std::uint8_t* px, std::size_t count, int tintR,
                   int tintG, int tintB) {
      const auto run = [&](auto fn) {
        for (std::size_t i = 0; i < count; ++i, px += RGBA_BYTES) {
          const Rgb8 o = fn(px[0], px[1], px[2]);
          px[0] = static_cast<std::uint8_t>(o.r);
          px[1] = static_cast<std::uint8_t>(o.g);
          px[2] = static_cast<std::uint8_t>(o.b);
        }
      };
      switch (mode) {
        case FilterMode::BW:
          run([](int r, int g, int b) { return filterPixel(FilterMode::BW, r, g, b, 0, 0, 0); });
          return;
        case FilterMode::SEPIA:
          run([](int r, int g, int b) { return filterPixel(FilterMode::SEPIA, r, g, b, 0, 0, 0); });
          return;
        case FilterMode::INVERT:
          run([](int r, int g, int b) { return filterPixel(FilterMode::INVERT, r, g, b, 0, 0, 0); });
          return;
        case FilterMode::CUSTOM:
          run([&](int r, int g, int b) {
            return filterPixel(FilterMode::CUSTOM, r, g, b, tintR, tintG, tintB);
          });
          return;
        default:
          return;  // None / Contour: nothing to do here
      }
    }

  }  // namespace

  void applyFilterRGBA(FilterMode mode, std::uint8_t* data, std::size_t pixelCount,
                       int tintR, int tintG, int tintB) {
    if (mode == FilterMode::NONE || mode == FilterMode::CONTOUR || data == nullptr) return;
    filterRun(mode, data, pixelCount, tintR, tintG, tintB);
  }

  void applyFilterRows(FilterMode mode, std::uint8_t* data, int width, int y0, int y1,
                       int tintR, int tintG, int tintB) {
    if (mode == FilterMode::NONE || mode == FilterMode::CONTOUR || data == nullptr ||
        width <= 0)
      return;
    if (y0 < 0) y0 = 0;
    if (y1 <= y0) return;
    filterRun(mode, data + rgbaOffset(0, y0, width),
              static_cast<std::size_t>(y1 - y0) * static_cast<std::size_t>(width), tintR,
              tintG, tintB);
  }

  // The pinned integer Sobel; browser/js/core/contourFilter.js reimplements exactly this.
  // The luma weights sum to 10000, so a uint8_t plane holds it; the Sobel sums run in int.
  void buildLumaRows(const std::uint8_t* data, int width, int height, int y0, int y1,
                     std::uint8_t* luma) {
    if (data == nullptr || luma == nullptr || width <= 0 || height <= 0) return;
    y0 = std::max(0, y0);
    y1 = std::min(height, y1);
    for (int y = y0; y < y1; ++y) {
      const std::uint8_t* px = data + rgbaOffset(0, y, width);
      std::uint8_t* out = luma + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
      for (int x = 0; x < width; ++x, px += RGBA_BYTES)
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
      // Reads one row OUTSIDE [y0, y1) on each side: the whole luma plane exists first.
      const std::size_t w = static_cast<std::size_t>(width);
      const std::uint8_t* up = luma + static_cast<std::size_t>(std::max(y - 1, 0)) * w;
      const std::uint8_t* mid = luma + static_cast<std::size_t>(y) * w;
      const std::uint8_t* dn = luma + static_cast<std::size_t>(std::min(y + 1, height - 1)) * w;
      std::uint8_t* px = data + rgbaOffset(0, y, width);
      const auto write = [&](int x, int gx, int gy) {
        const int mag = std::min(CHANNEL_MAX, std::abs(gx) + std::abs(gy));
        const auto v = static_cast<std::uint8_t>(CHANNEL_MAX - mag);  // dark edge on white
        std::uint8_t* p = px + static_cast<std::size_t>(x) * RGBA_BYTES;
        p[0] = v;
        p[1] = v;
        p[2] = v;
      };
      // Only the first/last column needs the x clamp; the interior loop stays branch-free.
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

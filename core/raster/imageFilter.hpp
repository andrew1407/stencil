#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Pure image-filter math over RGB components / interleaved RGBA8 buffers. Port of
// browser/js/core/renderer.js drawImageWithFilter + #applyTintFilter; the desktop
// routes its per-pixel work through it too, so every front-end's output is identical.
namespace stencil::core {

  // Mirrors the browser's `imageFilter` strings. The values cross the wasm ABI as int
  // codes: never reorder, only append.
  enum class FilterMode { NONE, BW, SEPIA, CUSTOM, INVERT, CONTOUR };

  // Any unknown non-"none" value maps to Custom (renderer.js's else branch).
  FilterMode filterModeFromString(const std::string& mode);

  struct Rgb8 {
    int r = 0;
    int g = 0;
    int b = 0;
  };

  // tint* are read only for Custom (grayscale, then dark -> tint, light -> white). Bw is
  // Rec. 709 luma, Sepia the CSS sepia(100%) matrix clamped; Contour returns the source.
  Rgb8 filterPixel(FilterMode mode, int r, int g, int b, int tintR, int tintG,
                   int tintB);

  // In place on a browser ImageData.data layout; alpha untouched. None and Contour
  // (which needs dimensions — applyContourRGBA) are no-ops.
  void applyFilterRGBA(FilterMode mode, std::uint8_t* data, std::size_t pixelCount,
                       int tintR, int tintG, int tintB);

  // Sobel edges in place, dark on white, alpha kept. Pinned integer-only for the JS fallback:
  // luma (2126*r + 7152*g + 722*b)/10000 truncating, 3x3 gx/gy clamped, 255 - min(255,|gx|+|gy|).
  void applyContourRGBA(std::uint8_t* data, int width, int height);

  // Half-open [y0, y1) row slices for an adapter's thread pool (core owns no threading).
  // applyFilterRows has no height (its twin counts PIXELS), so y1 is trusted.
  void applyFilterRows(FilterMode mode, std::uint8_t* data, int width, int y0, int y1,
                       int tintR, int tintG, int tintB);

  // Contour in two passes over a caller-owned width*height `luma` plane. The Sobel pass reads one
  // row OUTSIDE its range each side, so EVERY luma row must exist before ANY sobelRows call.
  void buildLumaRows(const std::uint8_t* data, int width, int height, int y0, int y1,
                     std::uint8_t* luma);
  void sobelRows(const std::uint8_t* luma, std::uint8_t* data, int width, int height,
                 int y0, int y1);

  // `scratch` is the luma plane, resized as needed, so a batch reuses one allocation.
  void applyContourRGBA(std::uint8_t* data, int width, int height,
                        std::vector<std::uint8_t>& scratch);

}

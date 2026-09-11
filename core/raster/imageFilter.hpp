#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Pure image-filter math, shared by the Qt desktop and the WebAssembly browser
// build. Port of browser/js/core/renderer.js drawImageWithFilter (~9) +
// #applyTintFilter (~164), which the desktop previously hand-rolled inside
// gui/canvasWidget.cpp rebuildFilteredImage. GUI-free, STL-only: it operates on
// plain RGB components / interleaved RGBA8 buffers, never on QImage or a canvas.
//
// The arithmetic here is the canonical, tested implementation; both front-ends
// route their per-pixel work through it so the bw / sepia / duotone-tint output
// stays identical by construction.
namespace stencil::core {

  // Image filter modes. Mirrors the browser's `imageFilter` strings
  // (none | bw | sepia | custom | invert | contour). The numeric values cross
  // the wasm ABI as int codes, so existing entries must never be reordered —
  // new modes are appended (Invert = 4, Contour = 5).
  enum class FilterMode { None, Bw, Sepia, Custom, Invert, Contour };

  // Map a browser filter string to a FilterMode. "bw"/"sepia"/"invert"/"contour"
  // map exactly; any other non-"none" value maps to Custom (matching
  // renderer.js's else branch and the desktop's prior behavior).
  // "none" / empty -> None.
  FilterMode filterModeFromString(const std::string& mode);

  // A single filtered pixel's color channels (0–255).
  struct Rgb8 {
    int r = 0;
    int g = 0;
    int b = 0;
  };

  // Transform one pixel through `mode`. (r,g,b) are the source channels; the
  // tint* channels are used only for FilterMode::Custom (the duotone target
  // color). None returns the source unchanged. Bw uses Rec. 709 luma; Sepia the
  // CSS sepia(100%) matrix clamped to [0,255]; Custom grayscales then maps dark
  // pixels toward the tint and light pixels toward white; Invert flips every
  // channel (255 - c). Contour returns the source unchanged — it needs
  // dimensions, use applyContourRGBA below.
  Rgb8 filterPixel(FilterMode mode, int r, int g, int b, int tintR, int tintG,
                   int tintB);

  // Apply `mode` in place to an interleaved RGBA8 buffer of `pixelCount` pixels
  // (byte order R,G,B,A — exactly a browser ImageData.data layout). The alpha
  // byte of every pixel is left untouched. None is a no-op, and so is Contour —
  // it needs dimensions, use applyContourRGBA below. This is the entry point
  // the WebAssembly browser build calls on a canvas's ImageData.
  void applyFilterRGBA(FilterMode mode, std::uint8_t* data, std::size_t pixelCount,
                       int tintR, int tintG, int tintB);

  // Sobel edge detection ("contour") in place on an interleaved RGBA8 buffer of
  // width x height pixels: dark edges on a white page, alpha preserved. The math
  // is pinned integer-only so the browser's JS fallback stays byte-identical:
  // a Rec. 709 luma plane via `(2126*r + 7152*g + 722*b) / 10000` (truncating),
  // the 3x3 Sobel gx/gy over edge-replicated (clamped) coordinates,
  // `mag = min(255, |gx| + |gy|)`, and output `r = g = b = 255 - mag`.
  // Degenerate input (null buffer, non-positive dimension) is a no-op; 1x1 /
  // 1xN images work via the clamping (gx/gy vanish where neighbors are equal).
  void applyContourRGBA(std::uint8_t* data, int width, int height);

  // Half-open [y0, y1) row slices of the ops above, so a native adapter can spread an
  // image over its own thread pool (core owns no threading policy). Each whole-image
  // entry point runs the same kernel over the full range, so tiled and untiled output
  // is the same bytes. An empty or inverted range is a no-op.

  // applyFilterRGBA over rows [y0, y1) of a `width`-wide image; rows are disjoint.
  // There is no height here (its whole-image twin counts PIXELS, not rows), so y1 is
  // trusted: pass rows that exist. y0 < 0 clamps to 0.
  void applyFilterRows(FilterMode mode, std::uint8_t* data, int width, int y0, int y1,
                       int tintR, int tintG, int tintB);

  // Contour, split into its two passes over a caller-owned `luma` plane of width*height
  // bytes. The Sobel pass reads a 3x3 NEIGHBOURHOOD — one row outside its range on each
  // side — so EVERY luma row must be built before ANY sobelRows call: two separate
  // parallel phases, never interleaved per tile, or the tiles disagree at their seams.
  void buildLumaRows(const std::uint8_t* data, int width, int height, int y0, int y1,
                     std::uint8_t* luma);
  void sobelRows(const std::uint8_t* luma, std::uint8_t* data, int width, int height,
                 int y0, int y1);

  // applyContourRGBA with a caller-supplied luma plane (resized as needed), so a
  // batch reuses one allocation instead of one per image.
  void applyContourRGBA(std::uint8_t* data, int width, int height,
                        std::vector<std::uint8_t>& scratch);

}

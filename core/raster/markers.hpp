#pragma once
#include "colorNames.hpp"  // Rgba
#include "luma.hpp"
#include "models.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

// A line's point markers, row by row: per point a disc, then a 1 px ring contrasting with it, in point
// order as canvas drawPoint (browser/js/core/line/render.js) and Qt paint them, byte-equal to
// stamping each marker whole. Only rasterize.cpp draws with it.
namespace stencil::core::markers {

  // A marker's 1 px ring contrasts with its own fill: black from Rec. 709 luma 128 up, white
  // below. MARKER_RING.darkFromLuma in common/config/constants.json, drift-tested.
  inline constexpr int RING_DARK_FROM_LUMA = 128;
  inline Rgba ringFor(const Rgba& fill) {
    const bool light = luma::rec709Truncated(fill.r, fill.g, fill.b) >= RING_DARK_FROM_LUMA;
    return light ? Rgba{0, 0, 0, CHANNEL_MAX} : Rgba{CHANNEL_MAX, CHANNEL_MAX, CHANNEL_MAX, CHANNEL_MAX};
  }

  // `radius` > 0, already clamped; a marker whose ring cannot reach a pixel centre is skipped.
  // Returns the pass's work (pixels examined plus blends applied), the tests' measure of it.
  std::size_t drawMarkers(std::uint8_t* buf, int w, int h, const std::vector<Point>& pts,
                          double radius, const Rgba& fill, const Rgba& ring);

}  // namespace stencil::core::markers

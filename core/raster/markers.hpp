#pragma once
#include "colorNames.hpp"  // Rgba
#include "models.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

// A line's point markers, row by row: per point a disc, then the editor's 1 px ring, in point
// order as canvas drawPoint (browser/js/core/line/render.js) and Qt paint them, byte-equal to
// stamping each marker whole. Only rasterize.cpp draws with it.
namespace stencil::core::markers {

  // `radius` > 0, already clamped; a marker whose ring cannot reach a pixel centre is skipped.
  // Returns the pass's work (pixels examined plus blends applied), the tests' measure of it.
  std::size_t drawMarkers(std::uint8_t* buf, int w, int h, const std::vector<Point>& pts,
                          double radius, const Rgba& fill, const Rgba& ring);

}  // namespace stencil::core::markers

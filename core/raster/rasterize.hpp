#pragma once
#include "colorNames.hpp"  // Rgba
#include "models.hpp"

#include <cstdint>

// Software rasteriser burning models.hpp lines into an RGBA8 image in place, for the
// headless CLI (the GUIs draw with Qt/canvas). Pure geometry, no glyphs; "transparent"
// or unparseable colours are skipped.
namespace stencil::core {

  void rasterizeLine(std::uint8_t* buf, int w, int h, const Line& line);

  // NOT splittable per line for a thread pool: blends overlap and are order-dependent,
  // so two lines touching one pixel must land in list order. Only the fill splits.
  void rasterizeLines(std::uint8_t* buf, int w, int h, const Lines& lines);

  // Even-odd scanline fill over the half-open rows [rowFrom, rowTo). A scanline writes
  // only its own row, so ranges may run concurrently.
  void fillPolygonRows(std::uint8_t* buf, int w, int h, const std::vector<Point>& pts,
                       const Rgba& c, int rowFrom, int rowTo);

}

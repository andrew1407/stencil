#pragma once
#include "colorNames.hpp"  // Rgba
#include "models.hpp"

#include <cstdint>

// Software rasteriser burning models.hpp lines into an RGBA8 image in place, for the
// headless CLI (the GUIs draw with Qt/canvas). Pure geometry, no glyphs; "transparent"
// or unparseable colours are skipped.
namespace stencil::core {

  // One dash cycle in px along the path, whatever the thickness (canvas setLineDash). Twin:
  // STROKE_DASH in browser/js/config/constants.json, drift-tested in rasterize.test.cpp.
  struct DashPattern {
    double on;
    double off;
  };
  inline constexpr DashPattern DASHED{10.0, 5.0};
  inline constexpr DashPattern DOTTED{2.0, 5.0};

  // Widest stroke burned, px: twice the 16384 px decode limit. A wider layout value clamps.
  inline constexpr double MAX_STROKE_THICKNESS = 32768.0;

  void rasterizeLine(std::uint8_t* buf, int w, int h, const Line& line);

  // Even-odd scanline fill over the half-open rows [rowFrom, rowTo). A scanline writes
  // only its own row, so ranges may run concurrently.
  void fillPolygonRows(std::uint8_t* buf, int w, int h, const std::vector<Point>& pts,
                       const Rgba& c, int rowFrom, int rowTo);

}

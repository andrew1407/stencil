#pragma once
#include "colorNames.hpp"  // Rgba
#include "models.hpp"

#include <cstdint>

// Software rasteriser that burns layout lines into an RGBA8 image, so the headless
// CLI can emit a finished annotated picture (the GUI apps draw with Qt/canvas; this
// is their codec-free, STL-only equivalent). Draws the models.hpp Line shapes:
// optional closed-polygon fill, a thick/dashed/dotted polyline, and round point
// points, alpha-blended in place. Pure geometry — no text/glyph rendering. Colours
// are resolved via colorNames.hpp; "transparent" or unparseable colours are skipped.
namespace stencil::core {

  // Draw one line onto an RGBA8 buffer of w x h (byte order R,G,B,A), in place.
  void rasterizeLine(std::uint8_t* buf, int w, int h, const Line& line);

  // Draw every line in order (later lines paint over earlier ones). NOT splittable per
  // line for an adapter's thread pool: the blends overlap and are order-dependent, so
  // two lines touching one pixel must land in list order. Only the fill below splits.
  void rasterizeLines(std::uint8_t* buf, int w, int h, const Lines& lines);

  // Even-odd scanline fill of a closed polygon over the half-open rows [rowFrom, rowTo),
  // clamped to the buffer and the polygon's bounds. A scanline writes only its own row,
  // so ranges may run concurrently; rasterizeLine calls it over [0, h).
  void fillPolygonRows(std::uint8_t* buf, int w, int h, const std::vector<Point>& pts,
                       const Rgba& c, int rowFrom, int rowTo);

}

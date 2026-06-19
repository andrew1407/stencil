#pragma once
#include "models.hpp"

#include <cstdint>

// Software rasteriser that burns layout lines into an RGBA8 image, so the headless
// CLI can emit a finished annotated picture (the GUI apps draw with Qt/canvas; this
// is their codec-free, STL-only equivalent). Draws the models.hpp Line shapes:
// optional closed-polygon fill, a thick/dashed/dotted polyline, and round point
// markers, alpha-blended in place. Pure geometry — no text/glyph rendering. Colours
// are resolved via colorNames.hpp; "transparent" or unparseable colours are skipped.
namespace stencil::core {

  // Draw one line onto an RGBA8 buffer of w x h (byte order R,G,B,A), in place.
  void rasterizeLine(std::uint8_t* buf, int w, int h, const Line& line);

  // Draw every line in order (later lines paint over earlier ones).
  void rasterizeLines(std::uint8_t* buf, int w, int h, const Lines& lines);

}

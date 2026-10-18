#pragma once
#include <string>
#include <vector>

// The console logo as rows of text: common/icons/favicon.svg's rounded panel framing the
// signature polyline, rasterised from the favicon's own vertices into a cell grid. Port of
// the raster in cli/src/app/logo/mark.zig; the browser twin is js/core/format/logoBanner.js.
namespace stencil::core {

  // The polyline drawn inside the frame: the S, or a secret skin's P or M.
  enum class LogoMark { S, P, M };

  // Where the parts of the banner sit, in cells; every row is `indent + panelWidth + 2` wide
  // (the two side borders) and a boxed banner is `frameHeight + 2 + 2 * curveRows` rows tall.
  struct BannerLayout {
    int indent = 0;      // leading spaces before the left border
    int frameWidth = 0;  // the lighter inner frame holding the mark
    int frameHeight = 0;
    int margin = 0;      // the dark panel margin either side of the frame
    int panelWidth = 0;  // frameWidth + 2 * margin
    int curveRows = 0;   // the ╱ ╲ rows above and below the body (0 when compact)
    int labelRow = 0;    // the body row the wordmark sits beside
  };

  BannerLayout logoBannerLayout(bool compact);

  // The mark alone: frameHeight rows of frameWidth glyph cells (╱ ╲ ─ │ ● or a space).
  std::vector<std::string> rasteriseMark(LogoMark mark, bool compact);

  // The boxed banner: the rounded top, the body rows (border, margin, mark, margin, border)
  // and the rounded bottom, uncoloured and without the wordmark.
  std::vector<std::string> logoBannerRows(LogoMark mark, bool compact);

}

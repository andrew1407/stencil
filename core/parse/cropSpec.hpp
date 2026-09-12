#pragma once
#include "cropGeometry.hpp"

#include <optional>
#include <string>

// The CLI's crop string, e.g. "x1 = 90 x2 = 200, y1 = 90 y2 = 567", each edge a length
// token (lengthTokens.hpp): the headless twin of the browser's stencil.crop({x1,x2,y1,y2}).
namespace stencil::core {

  // `valid` is false on an unknown key or malformed structure — a present-but-unparseable
  // token surfaces later, in resolveCropRect. `aspect` is a "W:H" ratio applied after.
  struct CropSpec {
    std::optional<std::string> x1;
    std::optional<std::string> x2;
    std::optional<std::string> y1;
    std::optional<std::string> y2;
    std::optional<std::string> aspect;
    bool valid = true;
  };

  // Pairs separate on spaces and/or commas; whitespace around '=' is optional.
  CropSpec parseCropSpec(const std::string& spec);

  struct CropResolveParams {
    double imageW = 0.0;     // effective (rotated) original width in px
    double imageH = 0.0;     // effective (rotated) original height in px
    double pxPerCmX = 0.0;   // px per cm on the X axis (canvasW / pageW)
    double pxPerCmY = 0.0;   // px per cm on the Y axis (canvasH / pageH)
    double pageWidth = 0.0;  // page size in cm, for album aspect derivation
    double pageHeight = 0.0;
  };

  // A missing edge defaults to the full image; with exactly one axis given the other
  // follows the page proportion (album = landscape). `aspect` then SHRINKS one dimension
  // about the centre (never grows; at least 1px). nullopt if any present token is
  // unparseable. The rect is normalized but NOT clamped — the caller clamps to the image.
  std::optional<CropRect> resolveCropRect(const CropSpec& spec,
                                          const CropResolveParams& params,
                                          bool album);

}

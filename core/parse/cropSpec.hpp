#pragma once
#include "cropGeometry.hpp"

#include <optional>
#include <string>

// The CLI's crop string, e.g. "x1 = 90 x2 = 200, y1 = 90 y2 = 567", each edge a length
// token (lengthTokens.hpp): the headless twin of stencil.crop({x1,x2,y1,y2}) in
// browser/js/console/api/cropApi.js.
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

  // A cm or percent edge's float noise, not a pixel: the slack an edge may pass its bound by.
  inline constexpr double CROP_EDGE_SLACK_PX = 1e-6;

  // A missing edge defaults to the full image; with one axis given the other follows the page
  // proportion. `aspect` SHRINKS about the centre (min 1px). A given edge outside [0, length]
  // fails; reversed edges are normalized, and nothing is clamped.
  std::optional<CropRect> resolveCropRect(const CropSpec& spec,
                                          const CropResolveParams& params,
                                          bool album);

}

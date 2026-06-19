#pragma once
#include "cropGeometry.hpp"

#include <optional>
#include <string>

// Parse + resolve the CLI's crop string, e.g. "x1 = 90 x2 = 200, y1 = 90 y2 = 567".
// Each edge value is a length token (px/cm/mm/in/%/bare — see lengthTokens.hpp). This
// is the headless equivalent of the browser console's stencil.crop({x1,x2,y1,y2}),
// including the album single-axis derivation. Pure, STL-only.
namespace stencil::core {

  // Edge tokens; any may be absent. `valid` is false when the string had an unknown
  // key or malformed structure (distinct from a present-but-unparseable token, which
  // surfaces later in resolveCropRect).
  struct CropSpec {
    std::optional<std::string> x1;
    std::optional<std::string> x2;
    std::optional<std::string> y1;
    std::optional<std::string> y2;
    bool valid = true;
  };

  // Parse "x1 = .. x2 = .. y1 = .. y2 = ..". Separators between pairs may be spaces
  // and/or commas; whitespace around '=' is optional. Unknown keys -> valid = false.
  CropSpec parseCropSpec(const std::string& spec);

  struct CropResolveParams {
    double imageW = 0.0;     // effective (rotated) original width in px
    double imageH = 0.0;     // effective (rotated) original height in px
    double pxPerCmX = 0.0;   // px per cm on the X axis (canvasW / pageW)
    double pxPerCmY = 0.0;   // px per cm on the Y axis (canvasH / pageH)
    double pageWidth = 0.0;  // page size in cm, for album aspect derivation
    double pageHeight = 0.0;
  };

  // Resolve to a pixel CropRect, mirroring browser stencil.crop(): a missing edge
  // defaults to the full image; when exactly one axis is given, the other is derived
  // from the page proportion (album = landscape). nullopt if any present token is
  // unparseable. The returned rect is normalized (positive width/height) but NOT
  // clamped — the caller clamps to the image as needed.
  std::optional<CropRect> resolveCropRect(const CropSpec& spec,
                                          const CropResolveParams& params,
                                          bool album);

}

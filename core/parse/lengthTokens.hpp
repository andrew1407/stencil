#pragma once
#include <optional>
#include <string>

// Length-token parsing. Port of browser/js/core/units.js (parseLengthToken /
// resolveAxisPx). Callers express a position/size as a bare number (a pixel DELTA),
// or a unit string: '3cm', '-4in', '50%', '-60%', '120px'. A leading '-' on a
// UNIT/PERCENT token means "measured from the END of the axis", NOT a negative
// length; on a bare number the '-' keeps its arithmetic meaning. Pure, no DOM.
namespace stencil::core {

  enum class LengthKind { Delta, Px, Cm, Percent };

  // Parsed token. For Delta the sign is folded into `value` and `fromEnd` is false;
  // for the absolute kinds `value` is non-negative and `fromEnd` carries the '-'.
  struct LengthToken {
    LengthKind kind = LengthKind::Delta;
    double value = 0.0;
    bool fromEnd = false;
  };

  // Parse a token. nullopt on unparseable input or an unknown unit suffix.
  std::optional<LengthToken> parseLengthToken(const std::string& token);

  // Resolve a token to an ABSOLUTE pixel coordinate on an axis:
  //   lengthPx  total axis length in px (image / page extent)
  //   pxPerCm   px per centimetre for cm/in conversion on this axis
  //   currentPx the current value, used as the base for a delta move
  // nullopt on unparseable input.
  std::optional<double> resolveAxisPx(const std::string& token, double lengthPx,
                                      double pxPerCm, double currentPx = 0.0);

}

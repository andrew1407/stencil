#pragma once
#include <optional>
#include <string>

// Length tokens. Port of browser/js/core/units.js (parseLengthToken / resolveAxisPx). A
// bare number is a pixel DELTA; '3cm', '-4in', '50%', '120px' are absolute, where a
// leading '-' means "measured from the END of the axis", not a negative length.
namespace stencil::core {

  enum class LengthKind { Delta, Px, Cm, Percent };

  // For Delta the sign is folded into `value`; for the absolute kinds `fromEnd` carries it.
  struct LengthToken {
    LengthKind kind = LengthKind::Delta;
    double value = 0.0;
    bool fromEnd = false;
  };

  std::optional<LengthToken> parseLengthToken(const std::string& token);

  // Absolute px on an axis of `lengthPx`; `currentPx` is the base of a delta move.
  std::optional<double> resolveAxisPx(const std::string& token, double lengthPx,
                                      double pxPerCm, double currentPx = 0.0);

}

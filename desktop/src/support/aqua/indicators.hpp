#pragma once
// The aqua check box and radio button as cached images (browser css/aqua/fields.css): a 14px glass
// cell, gel when on, the tick's long stroke leaving the box at its top-right as the browser's
// ::after does, the radio's 4px dot. QSS cannot paint past an indicator, so the image carries both.
#include <QColor>
#include <QString>

namespace stencil::support {

  // The image is the indicator: 18px square, the 14px cell at (0, 2), the overhang above and right.
  inline constexpr int AQUA_INDICATOR_PX = 18;

  struct AquaIndicatorColors {
    QColor glassTop, glassBottom, edge, gelHi, gelTop, gelBottom, gelEdge, tick;
  };

  enum class AquaIndicator { BOX, BOX_ON, BOX_DEAD_ON, RADIO, RADIO_ON, RADIO_DEAD_ON };

  // A cached PNG (and its @2x beside it) of `kind` in these colours; the path of the 1x file.
  QString aquaIndicatorPath(AquaIndicator kind, const AquaIndicatorColors& c);

}  // namespace stencil::support

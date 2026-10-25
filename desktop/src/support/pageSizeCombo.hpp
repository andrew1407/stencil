#pragma once
#include <QString>

// The page-format combo every page-size picker fills; browser twin: units.js pageFormatLabel.
class QComboBox;

namespace stencil::gui {

  // Labels "<name> (<w> × <h> <unit>)", ≤ 2 decimals trimmed (the browser dropdown's
  // contract); item DATA is the canonical "custom"/"A4". Re-invoking re-renders labels only.
  void fillPageSizeCombo(QComboBox* combo, bool includeCustom,
                         const QString& units = QStringLiteral("cm"));

}

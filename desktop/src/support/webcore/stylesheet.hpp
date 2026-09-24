#pragma once
// The webcore skin's sheet and palette: the app sheet with resources/webcore.qss laid over it,
// and the Palette the hand-painted chrome takes while the skin is on. Twin of
// browser/css/webcore/.
#include "theme.hpp"

#include <QString>

namespace stencil::support {

  // buildStylesheet(dark, accent) plus the overlay, its %WC_*% tokens filled.
  QString buildWebcoreStylesheet(bool dark, const QString& accentKey);

  // The overlay alone, filled: what the drift guard checks for orphan tokens.
  QString webcoreOverlay(bool dark, const QString& accentKey = QString());

  stencil::gui::Palette webcorePalette(bool dark);

}  // namespace stencil::support

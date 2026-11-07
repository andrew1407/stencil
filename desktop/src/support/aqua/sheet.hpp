#pragma once
// The aqua skin's sheet and palette: the app sheet with resources/qss/aqua/ laid over it, and
// the Palette the hand-painted chrome takes while the skin is on. Twin of browser/css/aqua/.
#include "theme.hpp"

#include <QString>

namespace stencil::support {

  // buildStylesheet(dark, accent) plus the overlay, its %AQ_*% tokens filled.
  QString buildAquaStylesheet(bool dark, const QString& accentKey);

  // The overlay alone, filled: what the drift guard checks for orphan tokens.
  QString aquaOverlay(bool dark, const QString& accentKey = QString());

  stencil::gui::Palette aquaPalette(bool dark);

  // A base theme token through aqua.json `aliases`, accent-tinted like the palette; invalid if not aliased.
  QColor aquaToken(const char* css, bool dark);

}  // namespace stencil::support

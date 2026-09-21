#pragma once

// Memoised lookups for the canvas paint path, shared by the Canvas*.cpp partials:
// themePalette() rebuilds 23 colours and cssColor() reparses a string, and the paint
// path wants both per line per frame. One cache each, in one TU.

#include "theme.hpp"

#include <QColor>
#include <QPoint>
#include <QRect>
#include <QString>
#include <string>

namespace stencil::gui {

  // The finished canvas palette for these theme tokens + Settings highlight colours.
  const Palette& paintPalette(bool dark, const QString& accentKey, const QColor& selGlow,
                              const QColor& hoverRing);

  // A stored CSS colour string, parsed once.
  const QColor& paintColor(const std::string& css);

  // The dashed rubber band's own pixels, padded for its 1px pen + antialiasing.
  inline QRect bandRect(const QPoint& a, const QPoint& b) {
    return QRect(a, b).normalized().adjusted(-2, -2, 2, 2);
  }

}  // namespace stencil::gui

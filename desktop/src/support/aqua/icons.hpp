#pragma once
// The aqua skin's icons (common/config/iconsAqua.json over the qrc): glossy colour art on a
// 32 grid, colour baked in. Twin of browser/js/ui/aqua/icons.js.
#include <QImage>
#include <QString>

namespace stencil::support {

  bool hasAquaIcon(const QString& name);

  // A complete <svg> document, or empty for an unknown name (the iconSet hook).
  QString aquaIconSvg(const QString& name);

  // The glyph as a raster, `scale` device pixels a 1/16 of its side; null for an unknown name.
  QImage aquaIconImage(const QString& name, bool dark, int scale);

}  // namespace stencil::support

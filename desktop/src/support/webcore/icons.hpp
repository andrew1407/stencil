#pragma once
// The skin's icons (browser/js/config/iconsWebcore.json over the qrc): a palette map becomes
// one <rect> per lit run on a 16-grid, colour baked in. Twin of browser/js/ui/webcore/icons.js.
#include <QColor>
#include <QImage>
#include <QString>

namespace stencil::support {

  bool hasPixelIcon(const QString& name);

  // A complete crisp-edged <svg> document, or empty for an unknown name (the iconSet hook).
  // The dark face has its own ink (config `paletteDark`), asked from skinPrefs.
  QString pixelIconSvg(const QString& name);

  // The header mark with its ring in `frame`: the one part of the art that takes the accent,
  // as the app's own mark's frame does.
  QString pixelLogoSvg(const QColor& frame);

  // The glyph as a raster, `scale` device pixels a cell; null for an unknown name.
  QImage pixelIconImage(const QString& name, bool dark, int scale);

}  // namespace stencil::support

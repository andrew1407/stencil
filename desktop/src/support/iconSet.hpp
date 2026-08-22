#pragma once
#include <QString>

class QColor;
class QIcon;

// Shared inline-SVG icon set, ported from browser/js/ui/icons.js (and mirrored in
// extension/src/lib/icons.js): stroked line-art on a 24×24 grid. This is the
// desktop counterpart so the Qt app's toolbar / menus / buttons carry the SAME
// glyphs as the browser, instead of emoji or text-only labels.
//
// The browser draws these with `currentColor`, inheriting the button/text color.
// QSvgRenderer can't resolve `currentColor`, so themedIcon() bakes an explicit
// stroke/fill color into the SVG before rasterizing — callers re-request icons in
// the active text color whenever the theme flips (see MainWindow::applyTheme).
//
// Qt-only by design: must NOT live in core/ (GUI-free, wasm-compiled).
namespace stencil::gui {

  // Render the named icon as a QIcon filled with `color`, sized `size` px (square,
  // high-DPI aware). Unknown names return a null QIcon (a typo degrades to "no
  // glyph" rather than crashing). Results are cached by (name,color,size).
  // `shadow` lays a dark halo under the glyph — for white glyphs on a light accent.
  // Callers decide with theme.hpp accentNeedsGlyphShadow(). Cached per flag.
  // `dpr` overrides the screen's device-pixel ratio (0 = ask qApp). Tests pass it to
  // exercise the hi-dpi raster path on a 1x offscreen screen.
  QIcon themedIcon(const QString& name, const QColor& color, int size = 18, bool shadow = false,
                   qreal dpr = 0);

  // The same glyph turned `degrees` clockwise about its centre, re-rendered from the SVG
  // so the line-art stays crisp at any angle. Drives the collapse chevrons, which spin
  // half a turn rather than swapping to the opposite glyph (browser: animations.css
  // `#toggle-controls .ic`). Uncached — it is only called while an animation runs.
  QIcon rotatedIcon(const QString& name, const QColor& color, int size, qreal degrees,
                    qreal dpr = 0);

  // True if `name` is a known glyph — lets callers skip assigning an empty icon.
  bool hasIcon(const QString& name);

}

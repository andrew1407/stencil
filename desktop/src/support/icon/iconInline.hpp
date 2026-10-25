#pragma once
#include <QString>

// Icons outside a QIcon slot: one inlined into rich text, one spun on a button
// (browser `#toggle-controls .ic` in animations.css).
class QAbstractButton;
class QColor;

namespace stencil::gui {

  // A second call supersedes an in-flight spin; the animation dies with the button.
  void spinIcon(QAbstractButton* btn, const QString& name, const QColor& color, int size,
                qreal fromDeg, qreal toDeg, int ms);

  // `px` is LOGICAL pixels; the PNG is rasterised at the device pixel ratio for Retina.
  // `dpr` 0 = ask qApp (test seam: offscreen is always 1x). Empty for an unknown glyph.
  QString inlineIconHtml(const QString& name, const QColor& color, int px,
                         const QString& style = QString(), qreal dpr = 0);

}

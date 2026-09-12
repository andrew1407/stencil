#pragma once
#include <QColor>
#include <QString>

#include <QIcon>

// The icon set of browser/js/ui/icons.js. QSvgRenderer cannot resolve `currentColor`,
// so themedIcon() bakes the colour in; callers re-request on a theme flip.
namespace stencil::gui {

  // Cached by every argument; null for an unknown name. `dpr` 0 = ask qApp (test seam).
  QIcon themedIcon(const QString& name, const QColor& color, int size = 18,
                   qreal dpr = 0, int gap = 0);
  // Browser .btn-icon-text gap 6 − Qt's hard-coded 4.
  inline constexpr int LABEL_ICON_GAP = 2;
  inline QIcon labelIcon(const QString& name, const QColor& color, int size = 15) {
    return themedIcon(name, color, size, 0, LABEL_ICON_GAP);
  }

  // Uncached — only called while an animation runs.
  QIcon rotatedIcon(const QString& name, const QColor& color, int size, qreal degrees,
                    qreal dpr = 0);

  bool hasIcon(const QString& name);

  // Seams for iconMotion.hpp, which poses a sub-part of the markup per frame and
  // rasterizes it down the same path as the rest pose.
  QString iconMarkup(const QString& name);

  QString iconSvgDocument(const QString& inner, const QColor& color);

  // `withDisabled=false` skips the Disabled variant for per-frame posed icons.
  QIcon iconFromMarkup(const QString& inner, const QColor& color, int size,
                       qreal dpr, bool withDisabled = true, int gap = 0);

  // QIcon::cacheKey() survives the copy Qt makes for a button, so a hover handler can
  // recover the request without touching the call sites that assign icons.
  struct IconRequest {
    QString name;
    QColor color;
    int size = 18;
    qreal dpr = 1;
    int gap = 0;
  };
  bool iconRequestForKey(qint64 cacheKey, IconRequest* out);

}

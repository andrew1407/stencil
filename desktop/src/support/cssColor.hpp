#pragma once
// CSS colours, with alpha. Colours are stored as CSS because the browser writes them straight into
// a canvas context; core::parseColor (the 148-name CSS Level 4 table + #rgb/#rgba/#rrggbb/
// #rrggbbaa) resolves them on screen the same way rasterize.cpp does on export. QColor is the
// fallback only for forms core declines (`#rrrgggbbb`). Header-only, Q_OBJECT-free.
#include "colorNames.hpp"

#include <QColor>
#include <QString>

#include <string>

namespace stencil::gui {

  // core answers first so the screen and the export agree.
  inline QColor cssColor(const QString& text) {
    const QString t = text.trimmed();
    if (const auto c = core::parseColor(t.toStdString())) return QColor(c->r, c->g, c->b, c->a);
    return QColor(t);
  }
  inline QColor cssColor(const std::string& text) { return cssColor(QString::fromStdString(text)); }

  // `#rrggbb` while opaque, `#rrggbbaa` only once there is alpha to carry.
  inline QString cssName(const QColor& c) {
    if (!c.isValid()) return QString();
    if (c.alpha() >= 255) return c.name(QColor::HexRgb);
    return c.name(QColor::HexRgb) + QString("%1").arg(c.alpha(), 2, 16, QLatin1Char('0'));
  }

}  // namespace stencil::gui

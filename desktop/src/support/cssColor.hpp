#pragma once
// CSS colours, with alpha — the one place the app turns a stored colour string into a
// QColor and back.
//
// Colours are stored as CSS, because the browser writes them straight into a canvas
// context. The SAME parser the export path uses resolves them (core::parseColor, the
// 148-name CSS Level 4 table + #rgb/#rgba/#rrggbb/#rrggbbaa): QColor's own vocabulary
// differs — it has no `rebeccapurple` and no 4-digit hex, and its hex alpha comes
// FIRST — so parsing on screen with QColor drew a different colour from the one
// rasterize.cpp exported. QColor is kept only as the fallback for the forms core
// declines (`#rrrgggbbb`), so nothing that renders today stops rendering.
// Header-only, Q_OBJECT-free.
#include "colorNames.hpp"

#include <QColor>
#include <QString>

#include <string>

namespace stencil::gui {

  // A stored colour string → QColor, honouring a trailing CSS alpha byte. core answers
  // first so the screen and the export agree; what it declines falls through to QColor.
  inline QColor cssColor(const QString& text) {
    const QString t = text.trimmed();
    if (const auto c = core::parseColor(t.toStdString())) return QColor(c->r, c->g, c->b, c->a);
    return QColor(t);
  }
  inline QColor cssColor(const std::string& text) { return cssColor(QString::fromStdString(text)); }

  // …and back: `#rrggbb` while the colour is opaque (the form every surface reads), and
  // `#rrggbbaa` only once there is alpha to carry.
  inline QString cssName(const QColor& c) {
    if (!c.isValid()) return QString();
    if (c.alpha() >= 255) return c.name(QColor::HexRgb);
    return c.name(QColor::HexRgb) + QString("%1").arg(c.alpha(), 2, 16, QLatin1Char('0'));
  }

}  // namespace stencil::gui

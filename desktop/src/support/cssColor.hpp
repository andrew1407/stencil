#pragma once
// CSS colours, with alpha — the one place the app turns a stored colour string into a
// QColor and back.
//
// Colours are stored as CSS, because the browser writes them straight into a canvas
// context. `#rrggbb` Qt understands; `#rrggbbaa` it does not — QColor's hex forms put
// alpha FIRST — so the eight-digit form is unpacked and written back by hand here.
// core's parseHex checks only `size() < 7`, so the CLI and pystencil read the RGB and
// ignore the alpha. Header-only, Q_OBJECT-free.
#include <QColor>
#include <QString>

#include <string>

namespace stencil::gui {

  // A stored colour string → QColor, honouring a trailing CSS alpha byte. Anything Qt
  // already parses (names, #rgb, #rrggbb, rgba(...)) falls through to QColor untouched.
  inline QColor cssColor(const QString& text) {
    const QString t = text.trimmed();
    if (t.size() == 9 && t.startsWith(QLatin1Char('#'))) {
      bool ok = false;
      const uint v = t.mid(1).toUInt(&ok, 16);
      if (ok) {
        return QColor(int((v >> 24) & 0xFF), int((v >> 16) & 0xFF),
                      int((v >> 8) & 0xFF), int(v & 0xFF));
      }
    }
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

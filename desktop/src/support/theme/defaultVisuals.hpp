#pragma once
// The drawing and highlight defaults a fresh editor starts from (common/config/constants.json
// DEFAULT_VISUALS, plus HOLD_DRAW.delayMs as the browser's VIS_DEFAULTS adds it, through the qrc),
// read once; the browser's editorState.js and visuals modal read the same keys.
#include "accentDefaults.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace stencil::gui::defaultVisuals {

  // A build without the qrc; tests/support/theme/defaultVisuals holds these equal to the table.
  struct Table {
    QString color = QStringLiteral("#FFFF00");
    double thickness = 2.0;   // px
    double pointSize = 4.0;   // px
    QString style = QStringLiteral("solid");
    QString fillColor = QStringLiteral("#ffffff");   // a newly locked area's fill
    QString selGlow = QStringLiteral("#ffc800");
    QString hoverRing = QString::fromLatin1(DEFAULT_ACCENT_HEX);
    QString focusRing = QString::fromLatin1(DEFAULT_ACCENT_HEX);
    int holdDrawDelay = 500;   // ms
  };

  inline const Table& table() {
    static const Table t = [] {
      Table out;
      QFile f(QStringLiteral(":/config/constants.json"));
      if (!f.open(QIODevice::ReadOnly)) return out;
      const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
      const QJsonObject v = root.value(QLatin1String("DEFAULT_VISUALS")).toObject();
      auto str = [&v](const char* key, const QString& fallback) {
        const QString s = v.value(QLatin1String(key)).toString();
        return s.isEmpty() ? fallback : s;
      };
      auto num = [&v](const char* key, double fallback) {
        const double d = v.value(QLatin1String(key)).toDouble(0);
        return d > 0 ? d : fallback;
      };
      out.color = str("color", out.color);
      out.thickness = num("thickness", out.thickness);
      out.pointSize = num("pointSize", out.pointSize);
      out.style = str("style", out.style);
      out.fillColor = str("defaultFillColor", out.fillColor);
      out.selGlow = str("selGlowColor", out.selGlow);
      out.hoverRing = str("hoverRingColor", out.hoverRing);
      out.focusRing = str("focusRingColor", out.focusRing);
      const int hold = root.value(QLatin1String("HOLD_DRAW")).toObject().value(QLatin1String("delayMs")).toInt(0);
      if (hold > 0) out.holdDrawDelay = hold;
      return out;
    }();
    return t;
  }

}  // namespace stencil::gui::defaultVisuals

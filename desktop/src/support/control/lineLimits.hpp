#pragma once
// The line thickness and point-size ranges (common/config/constants.json LIMITS through the
// qrc), read once; the browser's toolbar and selected-line inputs clamp to the same keys.
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace stencil::support::lineLimits {

  // A build without the qrc; tests/support/control/lineLimits holds these equal to the table.
  struct Table {
    int thickMin = 1;   // px, a stroke's width
    int thickMax = 20;
    int pointMin = 1;   // px, a vertex dot's size
    int pointMax = 30;
  };

  inline const Table& table() {
    static const Table t = [] {
      Table out;
      QFile f(QStringLiteral(":/config/constants.json"));
      if (!f.open(QIODevice::ReadOnly)) return out;
      const QJsonObject limits =
          QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("LIMITS")).toObject();
      auto num = [&limits](const char* key, int fallback) {
        const int v = limits.value(QLatin1String(key)).toInt(0);
        return v > 0 ? v : fallback;
      };
      out.thickMin = num("thickMin", out.thickMin);
      out.thickMax = num("thickMax", out.thickMax);
      out.pointMin = num("pointMin", out.pointMin);
      out.pointMax = num("pointMax", out.pointMax);
      return out;
    }();
    return t;
  }

}  // namespace stencil::support::lineLimits

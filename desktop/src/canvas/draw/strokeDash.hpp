#pragma once
// The dash table, in px (browser/js/config/constants.json STROKE_DASH through the qrc), read
// once. The browser's renderer reads the same table; a QPen counts in pen widths, so the
// stroke divides by its own width.
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <string>

namespace stencil::gui::strokeDash {

  // A build without the qrc; tests/canvas/draw/strokeDash holds these equal to the table.
  inline const QList<qreal> FALLBACK_DASHED{10.0, 5.0};
  inline const QList<qreal> FALLBACK_DOTTED{2.0, 5.0};

  struct Table {
    QList<qreal> dashed = FALLBACK_DASHED;
    QList<qreal> dotted = FALLBACK_DOTTED;
  };

  inline const Table& table() {
    static const Table t = [] {
      Table out;
      QFile f(QStringLiteral(":/config/constants.json"));
      if (!f.open(QIODevice::ReadOnly)) return out;
      const QJsonObject dash =
          QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("STROKE_DASH")).toObject();
      auto read = [&dash](const char* key, QList<qreal>& into) {
        const QJsonArray a = dash.value(QLatin1String(key)).toArray();
        if (a.isEmpty()) return;
        into.clear();
        for (const QJsonValue& v : a) into << v.toDouble();
      };
      read("dashed", out.dashed);
      read("dotted", out.dotted);
      return out;
    }();
    return t;
  }

  // Empty for a solid line.
  inline const QList<qreal>& patternPx(const std::string& style) {
    static const QList<qreal> none;
    if (style == "dashed") return table().dashed;
    if (style == "dotted") return table().dotted;
    return none;
  }

  // A px pattern in a pen of `width`'s units (a cosmetic 0-width pen counts in device px).
  inline QList<qreal> inPenUnits(QList<qreal> px, qreal width) {
    const qreal unit = width > 0 ? width : 1.0;
    for (qreal& v : px) v /= unit;
    return px;
  }

  inline QList<qreal> penPattern(const std::string& style, qreal width) {
    return inPenUnits(patternPx(style), width);
  }

}  // namespace stencil::gui::strokeDash

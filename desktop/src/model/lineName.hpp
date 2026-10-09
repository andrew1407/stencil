#pragma once
// A line's label as stored (core::Line::name): trimmed and capped at constants.json
// LIMITS.lineNameMax through the qrc; '' = unnamed. Browser twin: core/line/selection.js lineNameOf.
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <string>

namespace stencil::model {

  // UTF-16 units, as the browser's String.slice counts them; 80 in a build without the qrc.
  inline int lineNameMax() {
    static const int n = [] {
      QFile f(QStringLiteral(":/config/constants.json"));
      if (!f.open(QIODevice::ReadOnly)) return 80;
      const int v = QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("LIMITS"))
                        .toObject().value(QLatin1String("lineNameMax")).toInt(0);
      return v > 0 ? v : 80;
    }();
    return n;
  }

  inline std::string lineNameOf(const QString& v) { return v.trimmed().left(lineNameMax()).toStdString(); }

}  // namespace stencil::model

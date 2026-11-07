#include "motionTable.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace stencil::support {

  const QJsonObject& motionTable() {
    static const QJsonObject ui = [] {
      QFile f(QStringLiteral(":/config/motion.json"));
      if (!f.open(QIODevice::ReadOnly)) return QJsonObject();
      return QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("ui")).toObject();
    }();
    return ui;
  }

  int motionTableMs(const char* key, int fallback) {
    const int v = motionTable().value(QLatin1String(key)).toInt(0);
    return v > 0 ? v : fallback;
  }

  double motionTableShare(const char* key, double fallback) {
    const double v = motionTable().value(QLatin1String(key)).toDouble(0.0);
    return v > 0 ? v : fallback;
  }

}  // namespace stencil::support

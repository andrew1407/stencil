#include "launchUriGrammar.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace stencil::gui::launchUri {

  const Grammar& grammar() {
    static const Grammar g = [] {
      Grammar out;
      QFile f(QStringLiteral(":/config/launchQuery.json"));
      if (!f.open(QIODevice::ReadOnly)) return out;
      const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
      const QJsonObject keys = root.value("keys").toObject();
      for (auto it = keys.begin(); it != keys.end(); ++it)
        for (const QJsonValue& w : it.value().toArray()) out.canonical.insert(w.toString(), it.key());
      const auto list = [&](const char* key) {
        QStringList l;
        for (const QJsonValue& v : root.value(QLatin1String(key)).toArray()) l << v.toString();
        return l;
      };
      out.secret = list("secretKeys");
      out.docks = list("chatDocks");
      out.trueWords = list("trueWords");
      out.falseWords = list("falseWords");
      const QJsonObject themes = root.value("themeWords").toObject();
      for (auto it = themes.begin(); it != themes.end(); ++it) out.themes.insert(it.key(), it.value().toString());
      return out;
    }();
    return g;
  }

}  // namespace stencil::gui::launchUri

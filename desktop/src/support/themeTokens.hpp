#pragma once
// The display-space seam and the theme-token canon, shared by the theme*.cpp TUs.
#include <QColor>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <array>

namespace stencil::gui {

  void ensureThemeResources();

  // The SAME sRGB hex the browser paints — encoding into Display P3 on macOS was a second
  // conversion (Qt's surface is colour-managed too). The one seam to change if a platform
  // ever hands over an unmanaged surface.
  inline QColor displayColor(const QColor& c) { return c; }

  // browser/js/config/themeTokens.json via app.qrc, keyed by CSS custom-property NAME —
  // a wrong key is an invalid QColor and fails loudly.
  inline const QHash<QString, QColor>& themeTokens(bool dark) {
    static const std::array<QHash<QString, QColor>, 2> tables = [] {
      ensureThemeResources();
      std::array<QHash<QString, QColor>, 2> t;
      QFile f(QStringLiteral(":/config/themeTokens.json"));
      if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject tokens =
            QJsonDocument::fromJson(f.readAll()).object().value("tokens").toObject();
        for (auto it = tokens.begin(); it != tokens.end(); ++it) {
          const QJsonObject o = it.value().toObject();
          t[0].insert(it.key(), QColor(o.value("light").toString()));
          t[1].insert(it.key(), QColor(o.value("dark").toString()));
        }
      }
      return t;
    }();
    return tables[dark ? 1 : 0];
  }

  // Not every canon token is a colour (--text-label is `inherit`); only the painted ones are read.
  inline QColor themeToken(const char* css, bool dark) {
    return displayColor(themeTokens(dark).value(QString::fromLatin1(css)));
  }

}  // namespace stencil::gui

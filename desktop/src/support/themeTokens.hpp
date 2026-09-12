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

  // The app paints the SAME sRGB hex the browser does — no colour-space encoding.
  // Encoding into Display P3 on macOS was a second conversion (Qt's surface is
  // colour-managed too) and washed every themed colour out. Kept as a named seam: if a
  // platform ever hands us an unmanaged surface, this is the one place to change.
  inline QColor displayColor(const QColor& c) { return c; }

  // The colour canon: browser/js/config/themeTokens.json via app.qrc, generated from
  // browser/css/theme.css (and pinned against it there). One table per theme, keyed by
  // the CSS custom-property NAME — the two positional QColor lists this replaced could
  // shift a slot silently; a wrong key here is an invalid QColor and fails loudly.
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

  // One token, in the display's space. Not every canon token is a colour (--text-label
  // is `inherit`, --sb-track `transparent`); only the ones the app paints are read.
  inline QColor themeToken(const char* css, bool dark) {
    return displayColor(themeTokens(dark).value(QString::fromLatin1(css)));
  }

}  // namespace stencil::gui

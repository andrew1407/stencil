#include "theme.hpp"
#include "themeTokens.hpp"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStyleHints>
#include <algorithm>
#include <array>
#include <cmath>
#ifdef Q_OS_LINUX
#include <QProcess>
#endif

// The shared-config canon rides in app.qrc. A pre-main caller (e.g. tipContent.cpp's
// static initializer) can reach the tables before the resource's own global
// initializer ran, so force registration on first read. Global scope for Q_INIT_RESOURCE.
static void ensureAppResources() { Q_INIT_RESOURCE(app); }

namespace stencil::gui {

  void ensureThemeResources() { ensureAppResources(); }

#ifdef Q_OS_LINUX
  namespace {
    // Runs a desktop-settings query and returns its trimmed stdout (empty on
    // failure). Used as the X11/GNOME fallback when Qt can't see the scheme.
    QString readCommand(const QString& program, const QStringList& args) {
      QProcess proc;
      proc.start(program, args);
      if (!proc.waitForFinished(1000)) {
        proc.kill();
        return QString();
      }
      return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    }
  }  // namespace
#endif

  // Port of the browser matchMedia('(prefers-color-scheme: dark)') check.
  // Qt 6.5+ QStyleHints::colorScheme() often returns Unknown on X11/GNOME, so
  // fall back to the freedesktop portal, then the GNOME setting directly.
  bool systemPrefersDark() {
#ifdef Q_OS_LINUX
    // Under xcb on GNOME, QStyleHints::colorScheme() wrongly reports Light (not
    // Unknown), so consult the portal + gsettings FIRST and only trust Qt's hint
    // when neither answers. Linux-only: elsewhere Qt's hint is reliable, and
    // stray PATH gdbus/gsettings binaries don't reflect the OS appearance.

    // freedesktop portal (color-scheme: 1 = prefer dark, 2 = prefer light,
    // 0 = no preference). Returned as "(<<uint32 1>>,)".
    const QString portal = readCommand(
        "gdbus",
        {"call", "--session", "--dest", "org.freedesktop.portal.Desktop",
         "--object-path", "/org/freedesktop/portal/desktop", "--method",
         "org.freedesktop.portal.Settings.Read", "org.freedesktop.appearance",
         "color-scheme"});
    if (portal.contains("uint32 1")) return true;
    if (portal.contains("uint32 2")) return false;

    // GNOME setting directly (e.g. "'prefer-dark'" / "'default'").
    const QString gnome = readCommand(
        "gsettings", {"get", "org.gnome.desktop.interface", "color-scheme"});
    if (gnome.contains("dark", Qt::CaseInsensitive)) return true;
    if (gnome.contains("light", Qt::CaseInsensitive) ||
        gnome.contains("default", Qt::CaseInsensitive))
      return false;
#endif

    // No desktop answer -> trust Qt's hint as a last resort. colorScheme() /
    // Qt::ColorScheme arrived in Qt 6.5; on older Qt we have no hint to consult,
    // so default to light.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    return false;
#endif
  }

  // Port of the browser theme toggle's tri-state resolution.
  bool resolveDark(const QString& mode) {
    if (mode == "dark") return true;
    if (mode == "light") return false;
    return systemPrefersDark();  // "system" (default)
  }

  // The selectable brand-accent presets, parsed once from the shared canon
  // (browser/js/config/accents.json via app.qrc — same list the browser and
  // extension read); violet is the default. --accent-2 derives from the primary below.
  const std::vector<AccentPreset>& accentPresets() {
    static const std::vector<AccentPreset> presets = [] {
      ensureAppResources();
      std::vector<AccentPreset> v;
      QFile f(":/config/accents.json");
      if (f.open(QIODevice::ReadOnly)) {
        for (const auto& av : QJsonDocument::fromJson(f.readAll()).array()) {
          const QJsonObject o = av.toObject();
          v.push_back({o.value("key").toString(), o.value("label").toString(),
                       o.value("hex").toString()});
        }
      }
      return v;
    }();
    return presets;
  }

  namespace {

    // sRGB transfer function: encoded 0..1 -> linear light (CSS/WCAG, not Rec.709 luma).
    double srgbToLinear(double v) {
      return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    }

    [[maybe_unused]] QColor encodeDisplayP3(const QColor& c) {
#ifdef Q_OS_MACOS
      if (!c.isValid()) return c;
      const auto toCurve = [](double v) {
        v = std::clamp(v, 0.0, 1.0);
        return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
      };
      const double r = srgbToLinear(c.redF()), g = srgbToLinear(c.greenF()),
                   b = srgbToLinear(c.blueF());
      // sRGB -> XYZ(D65) -> Display P3 linear, folded into one matrix.
      constexpr double m[3][3] = {{0.822462, 0.177538, 0.000000},
                                  {0.033194, 0.966806, 0.000000},
                                  {0.017083, 0.072397, 0.910520}};
      QColor out = QColor::fromRgbF(toCurve(m[0][0] * r + m[0][1] * g + m[0][2] * b),
                                    toCurve(m[1][0] * r + m[1][1] * g + m[1][2] * b),
                                    toCurve(m[2][0] * r + m[2][1] * g + m[2][2] * b));
      out.setAlpha(c.alpha());
      return out;
#else
      return c;
#endif
    }
  }  // namespace

  QColor accentPrimary(const QString& accentKey) {
    for (const AccentPreset& a : accentPresets())
      if (a.key == accentKey) return displayColor(QColor(a.hex));
    // A custom accent stored as a hex string (set via the desktop logo's double-click picker).
    if (accentKey.startsWith('#')) {
      QColor c(accentKey);
      if (c.isValid()) return displayColor(c);
    }
    return displayColor(QColor("#7c3aed"));  // unknown key -> violet (the default)
  }

  bool accentNeedsDarkGlyph(const QColor& accent) {
    if (!accent.isValid()) return false;
    // WCAG luminance → the two contrasts; the greater wins, as in accents.js.
    const double l = 0.2126 * srgbToLinear(accent.redF()) + 0.7152 * srgbToLinear(accent.greenF())
                     + 0.0722 * srgbToLinear(accent.blueF());
    return (l + 0.05) / 0.05 > 1.05 / (l + 0.05);
  }

  // Near-black, not black: the page ink, so it matches the app's other glyphs.
  QColor onAccentInk(const QColor& accent) {
    return accentNeedsDarkGlyph(accent) ? QColor("#1a1a1a") : QColor(Qt::white);
  }

  // The --accent-2 shade: darker in light mode, lighter in dark — the same
  // ratios as browser/css/theme.css (86% accent + 14% black / 78% + 22% white).
  QColor accentShade(const QColor& primary, bool dark) {
    return dark ? mixSrgb(primary, QColor(Qt::white), 0.22)
                : mixSrgb(primary, QColor(Qt::black), 0.14);
  }
}


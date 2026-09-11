#include "theme.hpp"
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
    // The app paints the SAME sRGB hex the browser does — no colour-space encoding.
    // Encoding into Display P3 on macOS was a second conversion (Qt's surface is
    // colour-managed too) and washed every themed colour out. Kept as a named seam: if a
    // platform ever hands us an unmanaged surface, this is the one place to change.
    QColor displayColor(const QColor& c) { return c; }

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

  namespace {
    // The colour canon: browser/js/config/themeTokens.json via app.qrc, generated from
    // browser/css/theme.css (and pinned against it there). One table per theme, keyed by
    // the CSS custom-property NAME — the two positional QColor lists this replaced could
    // shift a slot silently; a wrong key here is an invalid QColor and fails loudly.
    const QHash<QString, QColor>& themeTokens(bool dark) {
      static const std::array<QHash<QString, QColor>, 2> tables = [] {
        ensureAppResources();
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
    QColor themeToken(const char* css, bool dark) {
      return displayColor(themeTokens(dark).value(QString::fromLatin1(css)));
    }
  }  // namespace

  QColor dangerHover(bool dark) { return themeToken("--danger-2", dark); }

  QColor infoBackground(bool dark) { return themeToken("--bg-info", dark); }

  Palette themePalette(bool dark, const QString& accentKey) {
    Palette p;
    p.bgPage = themeToken("--bg-page", dark);
    p.bgContainer = themeToken("--bg-container", dark);
    p.bgControls = themeToken("--bg-controls", dark);
    p.bgSelPanel = themeToken("--bg-sel-panel", dark);
    p.borderMain = themeToken("--border-main", dark);
    p.borderCanvas = themeToken("--border-canvas", dark);
    p.borderSel = themeToken("--border-sel", dark);
    p.textMain = themeToken("--text-main", dark);
    p.textMuted = themeToken("--text-muted", dark);
    p.textSelLabel = themeToken("--text-sel-label", dark);
    p.bgSelBtn = themeToken("--bg-sel-btn", dark);
    p.bgSelBtnHov = themeToken("--bg-sel-btn-hov", dark);
    p.textSelBtn = themeToken("--text-sel-btn", dark);
    p.inputBg = themeToken("--input-bg", dark);
    p.inputText = themeToken("--input-text", dark);
    p.danger = themeToken("--danger", dark);
    p.bgCoordHover = themeToken("--bg-coord-hover", dark);
    p.warning = themeToken("--warning", dark);
    p.disabledText = themeToken("--disabled-text", dark);
    // The two that are NOT theme.css tokens: constants.json DEFAULT_VISUALS, the
    // canvas highlight styles Settings lets the user repaint (fileStore defaults).
    p.selGlow = displayColor(QColor("#ffc800"));
    p.hoverRing = displayColor(QColor("#7c3aed"));
    // The accent-derived three. --accent / --text-key are in the canon too, but only as
    // the violet default: the chosen accent is what the app actually wears.
    const QColor accent = accentPrimary(accentKey);   // already display-space
    p.accent = accent;
    p.textKey = accentShade(accent, dark);
    p.onAccent = onAccentInk(accent);
    return p;
  }

  namespace {
    // The combo-box caret, drawn once per colour into the app's cache dir and referenced
    // from the stylesheet by path — the only way to put a shape in a QSS sub-control.
    // Written at 2x as well ("@2x", Qt's own high-DPI naming) so it stays sharp on retina.
    QString caretImagePath(const QColor& colour) {
      const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
      QDir().mkpath(dir);
      const QString base = dir + "/caret-" + colour.name(QColor::HexRgb).mid(1);
      const QString path = base + ".png";
      if (!QFile::exists(path)) {
        for (const int scale : {1, 2}) {
          const int px = 9 * scale;
          QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
          img.fill(Qt::transparent);
          QPainter p(&img);
          p.setRenderHint(QPainter::Antialiasing);
          QPen pen(colour, 1.5 * scale);
          pen.setCapStyle(Qt::RoundCap);
          pen.setJoinStyle(Qt::RoundJoin);
          p.setPen(pen);
          const qreal m = 1.5 * scale, mid = px / 2.0;
          p.drawPolyline(QPolygonF({QPointF(m, mid - m / 1.5), QPointF(mid, px - m * 1.6),
                                    QPointF(px - m, mid - m / 1.5)}));
          p.end();
          img.save(scale == 1 ? path : base + "@2x.png", "PNG");
        }
      }
      return path;
    }

    // The stylesheet template, read once from qrc. resources/app.qss is the literal
    // this file used to carry inline, whitespace included — tests/pins/stylesheets.txt
    // hashes the finished sheet, so the move is provably byte-for-byte.
    const QString& stylesheetTemplate() {
      static const QString tpl = [] {
        ensureAppResources();
        QFile f(QStringLiteral(":/qss/app.qss"));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
      }();
      return tpl;
    }

    // Fill every %TOKEN% in one walk. A token the map doesn't know is left alone (the
    // only other `%` in the sheet sits inside a comment, and reads as no token at all).
    QString fillTokens(const QString& tpl, const QHash<QString, QString>& values) {
      static const QRegularExpression token(QStringLiteral("%[A-Z0-9_]+%"));
      QString out;
      out.reserve(tpl.size() + 2048);
      int pos = 0;
      QRegularExpressionMatchIterator it = token.globalMatch(tpl);
      while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const auto v = values.constFind(m.captured(0));
        if (v == values.constEnd()) continue;
        out.append(QStringView(tpl).mid(pos, m.capturedStart() - pos));
        out.append(*v);
        pos = m.capturedEnd();
      }
      out.append(QStringView(tpl).mid(pos));
      return out;
    }
  }

  // The browser's own thumb grey (css/theme.css --sb-thumb), lighter than the border
  // colour the bars used to borrow; under the pointer it takes the theme's accent, exactly
  // as the browser's --sb-thumb-hover: var(--accent) does (user decision).
  QColor canvasScrollThumb(bool dark) { return themeToken("--sb-thumb", dark); }
  QColor canvasScrollThumbHover(bool dark, const QString& accentKey) {
    return themePalette(dark, accentKey).accent;
  }

  QString buildStylesheet(bool dark, const QString& accentKey) {
    const Palette p = themePalette(dark, accentKey);
    auto c = [](const QColor& q) { return q.name(); };
    // rgba() string for accent tints — QSS has no color-mix, so the browser's
    // translucent accent hovers/focus rings are reproduced with alpha over the
    // surface beneath. Lets one accent recolour every hover state harmoniously.
    auto rgba = [](const QColor& q, double a) {
      return QString("rgba(%1,%2,%3,%4)")
          .arg(q.red())
          .arg(q.green())
          .arg(q.blue())
          .arg(a, 0, 'f', 3);
    };
    const QColor accent2 = p.textKey;  // the derived hover/active accent shade
    const bool darkGlyph = accentNeedsDarkGlyph(p.accent);   // which indicator mark to bake in
    // The dead-control surface (%DISABLED_BG%), and the caret drawn for one: muted pulled
    // halfway to that surface, so a disabled combo's arrow recedes with its text.
    const QColor disabledBg = dark ? p.bgControls : p.bgContainer.darker(108);
    const QColor caretDim = mixSrgb(p.textMuted, disabledBg, 0.55);
    // The scrollbars' unthemed fallback thumb (the pills read the same helper).
    const QColor sbThumb = canvasScrollThumb(dark);
    // Drag & drop hint strip (browser --bg-drop-hint/--border-hint, css/theme.css).
    // NB argument order: CSS color-mix(accent P%, base) == mixSrgb(base, accent, P).
    const QColor dropHintBg = dark ? mixSrgb(p.bgPage, p.accent, 0.16)
                                    : mixSrgb(QColor(Qt::white), p.accent, 0.09);
    const QColor dropHintBorder = dark ? mixSrgb(QColor("#2a2a2a"), p.accent, 0.50)
                                        : mixSrgb(QColor(Qt::white), p.accent, 0.35);
    // The coord-status readout's own border (browser --border-tooltip, css/theme.css) —
    // a distinct, slightly blue-grey hairline neither borderMain nor borderCanvas carries.
    const QColor borderTooltip = themeToken("--border-tooltip", dark);
    // --bg-info (browser theme.css): the neutral info fill — the projects rows' hover
    // wash and the inline-rename editor's chip/input surface.
    const QColor bgInfo = infoBackground(dark);
    // --success (browser theme.css), lifted toward the light on dark like the reds are.
    // The "today" outline in the expiration calendar is the only thing wearing it so far.
    const QColor success = themeToken("--success", dark);

    // One stylesheet covering the widgets the app uses. Tracks browser/css —
    // page backdrop, gradient controls, the brand accent, rounded inputs/lists,
    // accent-tinted hovers + focus rings — so the desktop matches the web look.
    // The sheet itself is resources/app.qss (embedded via app.qrc); only its %TOKEN%
    // values are computed here, and filled in ONE pass — the chain of ~40 chained
    // QString::replace() calls this grew out of rewrote the whole ~40 KB sheet once per
    // token, and previewAccent() rebuilds it for every accent row the pointer crosses.
    return fillTokens(stylesheetTemplate(), {
        {"%BTN_FLAT%", c(dark ? p.bgContainer.lighter(112) : p.bgContainer.darker(103))},
        // Geometry the code measures against (theme.hpp) — interpolated, never retyped.
        {"%MENU_PAD_R%", QString::number(kMenuItemRightPadPx)},
        {"%SEP_W%", QString::number(kDockSeparatorPx)},
        // The ink an accent-BACKED control paints its label and glyph in (theme.hpp
        // onAccentInk). The indicator marks are baked PNGs, so they come as a pair.
        {"%ON_ACCENT%", c(p.onAccent)},
        {"%TICK_IMG%", darkGlyph ? ":/icons/check-dark.png" : ":/icons/check.png"},
        {"%RADIO_IMG%", darkGlyph ? ":/icons/radio-dot-dark.png" : ":/icons/radio-dot.png"},
        // Dimmed accent fill for disabled accent buttons — still obviously part
        // of the accent group, just muted (see QToolButton[chatAccent]:disabled).
        {"%ACCENT_DIM%", rgba(p.accent, dark ? 0.38 : 0.30)},
        {"%ACCENT_SOFT2%", rgba(p.accent, dark ? 0.30 : 0.20)},
        {"%ACCENT_SOFT%", rgba(p.accent, dark ? 0.18 : 0.11)},
        {"%ACCENT_RING%", rgba(p.accent, 0.45)},
        // The line an input or icon button draws around itself: its OWN ink, well down —
        // full ink was hard and half still read heavy (user report, twice). A quarter keeps
        // the colour family while landing light-grey on the light theme and dark-grey on
        // the dark one, which is what a border should be in each.
        {"%UI_OUTLINE%", rgba(p.textMain, 0.25)},
        {"%ACCENT2%", c(accent2)},
        // Status reds mirror browser/css/theme.css --danger/--danger-2 (per theme).
        {"%SUCCESS_RING%", rgba(success, 0.55)},
        {"%DANGER2%", dangerHover(dark).name()},
        {"%DANGER%", c(p.danger)},
        // "Selected Line:" bar amber, mirroring browser/css/components.css
        // --bg-sel-panel / --border-sel / --text-sel-label (per theme). Routed through
        // the Palette (already display-space converted above), not re-hardcoded here —
        // a raw sRGB literal painted directly by Qt reads over-saturated on a wide-gamut
        // (P3) Mac display, same reason every other palette token goes through displayColor().
        {"%SEL_BG%", c(p.bgSelPanel)},
        {"%SEL_BORDER%", c(p.borderSel)},
        {"%SEL_LABEL%", c(p.textSelLabel)},
        {"%SEL_BTN_TEXT%", c(p.textSelBtn)},
        {"%SEL_BTN_HOV%", c(p.bgSelBtnHov)},
        {"%SEL_BTN%", c(p.bgSelBtn)},
        // The Deselect CTA's browser-hardcoded orange (layout.css .deselect-btn, no
        // light/dark variant) — same display-space conversion, or it reads noticeably
        // more saturated/brighter here than the color-managed browser rendering.
        {"%DESELECT_BG%", c(p.bgSelBtn)},
        {"%DESELECT_HOVER%", c(p.bgSelBtnHov)},
        {"%DISABLED_BG%", c(disabledBg)},
        {"%BG_PAGE%", c(p.bgPage)},
        {"%BG_CONTAINER%", c(p.bgContainer)},
        {"%BG_CONTROLS%", c(p.bgControls)},
        {"%BG_COORD_HOVER%", c(p.bgCoordHover)},
        // The keycap rows' hover: halfway from the container to the row-hover tint.
        {"%BG_ROW_HOVER_SOFT%", c(mixSrgb(p.bgContainer, p.bgCoordHover, 0.5))},
        // --bg-coord-even / --text-info (theme.css): the hotkey table's head and the
        // info rows' description ink (inherit on light).
        {"%BG_COORD_EVEN%", c(themeToken("--bg-coord-even", dark))},
        // --text-info is `inherit` on light, which is the main ink.
        {"%TEXT_INFO%", c(dark ? themeToken("--text-info", dark) : p.textMain)},
        {"%BG_INFO%", c(bgInfo)},
        {"%BG_DROP_HINT%", c(dropHintBg)},
        {"%BORDER_HINT%", c(dropHintBorder)},
        {"%BORDER_TOOLTIP%", c(borderTooltip)},
        {"%BORDER_CANVAS%", c(p.borderCanvas)},
        {"%BORDER%", c(p.borderMain)},
        {"%SB_THUMB%", c(sbThumb)},
        {"%TEXT%", c(p.textMain)},
        {"%CARET_DIM%", caretImagePath(caretDim)},
        {"%CARET%", caretImagePath(p.textMuted)},
        {"%MUTED%", c(p.textMuted)},
        // The captions ride the same softened ink as the outlines, a step stronger — the
        // fixed greys read as washed out beside them (user decision).
        {"%SECTIONTITLE%", rgba(p.textMain, 0.62)},
        {"%DISABLED_TEXT%", c(p.disabledText)},
        {"%ACCENT%", c(p.accent)},
        {"%INPUT_BG%", c(p.inputBg)},
        {"%INPUT_TEXT%", c(p.inputText)}});
  }

  // Build a QPalette from the same theme tokens the stylesheet uses, so menus,
  // popups and other native bits match. Set on qApp in applyTheme().
  QPalette buildQPalette(bool dark, const QString& accentKey) {
    const Palette p = themePalette(dark, accentKey);
    QPalette q;
    q.setColor(QPalette::Window, p.bgPage);
    q.setColor(QPalette::WindowText, p.textMain);
    q.setColor(QPalette::Base, p.inputBg);
    q.setColor(QPalette::AlternateBase, p.bgControls);
    q.setColor(QPalette::Text, p.inputText);
    q.setColor(QPalette::Button, p.bgControls);
    q.setColor(QPalette::ButtonText, p.textMain);
    q.setColor(QPalette::ToolTipBase, p.bgControls);
    q.setColor(QPalette::ToolTipText, p.textMain);
    q.setColor(QPalette::Highlight, p.accent);
    q.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    // Link = accent-2 (browser --text-key): key-chip text + hyperlinks, readable on every theme.
    q.setColor(QPalette::Link, p.textKey);
    q.setColor(QPalette::PlaceholderText, p.textMuted);
    // Mid = the same muted text: every `palette(mid)` stylesheet (section headers,
    // footer hints) reads it. Unset, Qt derives a grey from Button that vanishes
    // against the dark theme's dialogs.
    q.setColor(QPalette::Mid, p.textMuted);
    // Dark = the browser's --border-main: the hairline every hand-painted card outline
    // wants (projectsDialog's row cards). Mid is the muted TEXT, far too light for one.
    q.setColor(QPalette::Dark, p.borderMain);
    // The DISABLED group is the browser's --disabled-text — dimmer than the muted live
    // label, and what a rasterised disabled glyph is inked with (iconSet.cpp mutedInk).
    q.setColor(QPalette::Disabled, QPalette::Text, p.disabledText);
    q.setColor(QPalette::Disabled, QPalette::ButtonText, p.disabledText);
    q.setColor(QPalette::Disabled, QPalette::WindowText, p.disabledText);
    return q;
  }

}

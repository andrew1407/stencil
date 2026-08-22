#include "theme.hpp"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStandardPaths>
#include <QStyleHints>
#include <algorithm>
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
    // macOS composites raw pixels as if already in the DISPLAY's space, so on a
    // wide-gamut (P3) Mac an sRGB hex paints over-saturated vs the colour-managed
    // browser. Encode sRGB into P3 so both surfaces match; identity off macOS.
    QColor displayColor(const QColor& c) {
#ifdef Q_OS_MACOS
      if (!c.isValid()) return c;
      const auto toLinear = [](double v) {
        return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
      };
      const auto toCurve = [](double v) {
        v = std::clamp(v, 0.0, 1.0);
        return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
      };
      const double r = toLinear(c.redF()), g = toLinear(c.greenF()), b = toLinear(c.blueF());
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

  bool accentNeedsGlyphShadow(const QColor& accent) {
    if (!accent.isValid()) return false;
    // WCAG luminance → contrast of white against it. Threshold 3:1, same constant as
    // accents.js, so all three surfaces flip on the same accents.
    const auto toLinear = [](double c) {
      return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    const double l = 0.2126 * toLinear(accent.redF()) + 0.7152 * toLinear(accent.greenF())
                     + 0.0722 * toLinear(accent.blueF());
    return 1.05 / (l + 0.05) < 3.0;
  }

  namespace {
    // Linear sRGB mix, matching CSS color-mix(in srgb, a (1-t), b t) used by the
    // web themes for the accent shade + glows.
    QColor mixSrgb(const QColor& a, const QColor& b, double t) {
      return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t,
                              a.greenF() * (1 - t) + b.greenF() * t,
                              a.blueF() * (1 - t) + b.blueF() * t);
    }
    // The --accent-2 shade: darker in light mode, lighter in dark — the same
    // ratios as browser/css/theme.css (86% accent + 14% black / 78% + 22% white).
    QColor accentShade(const QColor& primary, bool dark) {
      return dark ? mixSrgb(primary, QColor(Qt::white), 0.22)
                  : mixSrgb(primary, QColor(Qt::black), 0.14);
    }
  }  // namespace

  // Values copied verbatim from browser/css/theme.css + the DEFAULT_VISUALS block
  // of browser/js/config/constants.json. accent + textKey (= --accent-2) are then
  // overridden from the chosen accentKey, so the whole app recolours.
  Palette themePalette(bool dark, const QString& accentKey) {
    static const Palette light{
        QColor("#f0f0f0"), QColor("#ffffff"), QColor("#f8f9fa"),
        QColor("#fff8e1"), QColor("#dddddd"), QColor("#dddddd"),
        QColor("#f0b429"), QColor("#000000"), QColor("#888888"),
        QColor("#7a5c00"), QColor("#6d28d9"), QColor("#ffffff"),
        QColor("#000000"), QColor("#7c3aed"), QColor("#d6293e"),
        QColor("#ffc800"), QColor("#7c3aed"),
    };
    static const Palette darkP{
        QColor("#1a1a1a"), QColor("#242424"), QColor("#2d2d2d"),
        QColor("#2e2a17"), QColor("#444444"), QColor("#555555"),
        QColor("#b8860b"), QColor("#e0e0e0"), QColor("#aaaaaa"),
        QColor("#e0b84a"), QColor("#9b6cf2"), QColor("#333333"),
        QColor("#e0e0e0"), QColor("#6d28d9"), QColor("#f0697a"),
        QColor("#ffc800"), QColor("#7c3aed"),
    };
    Palette p = dark ? darkP : light;
    // Every token goes to the display's space, so reds/golds match the browser too.
    for (QColor* f : {&p.bgPage, &p.bgContainer, &p.bgControls, &p.bgSelPanel, &p.borderMain,
                      &p.borderCanvas, &p.borderSel, &p.textMain, &p.textMuted, &p.textSelLabel,
                      &p.textKey, &p.inputBg, &p.inputText, &p.accent, &p.danger, &p.selGlow,
                      &p.hoverRing})
      *f = displayColor(*f);
    const QColor accent = accentPrimary(accentKey);   // already display-space
    p.accent = accent;
    p.textKey = accentShade(accent, dark);
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
    // Subtle vertical gradients give the toolbar/buttons depth without leaving
    // the flat browser aesthetic; the ends are tiny value steps off the surface.
    const QColor ctrlTop = dark ? p.bgControls.lighter(108) : p.bgControls.lighter(102);
    const QColor ctrlBot = dark ? p.bgControls : p.bgControls.darker(104);
    const QColor btnTop = dark ? p.bgContainer.lighter(118) : p.bgContainer;
    const QColor btnBot = dark ? p.bgContainer : p.bgContainer.darker(106);
    // The dead-control surface (%DISABLED_BG%), and the caret drawn for one: muted pulled
    // halfway to that surface, so a disabled combo's arrow recedes with its text.
    const QColor disabledBg = dark ? p.bgControls : p.bgContainer.darker(108);
    const QColor caretDim = mixSrgb(p.textMuted, disabledBg, 0.55);

    // One stylesheet covering the widgets the app uses. Tracks browser/css —
    // page backdrop, gradient controls, the brand accent, rounded inputs/lists,
    // accent-tinted hovers + focus rings — so the desktop matches the web look.
    return QString(R"(
      QMainWindow, QWidget#centralBackdrop { background: %BG_PAGE%; }

      /* ── Toolbars: gradient surface, hairline divider, rounded icon buttons ── */
      QToolBar {
        background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %CTRL_TOP%, stop:1 %CTRL_BOT%);
        border: 0; border-bottom: 1px solid %BORDER%;
        /* The browser's control rows sit on an 8px gap; 6 lands the icon buttons at the same
           visual rhythm once Qt's own button padding is counted. */
        spacing: 3px; padding: 4px 6px;
      }
      QToolBar::separator {
        width: 1px; background: %BORDER%; margin: 5px 6px; border-radius: 1px;
      }
      QToolBar QLabel { color: %MUTED%; background: transparent; padding: 0 2px; }
      /* The header row carries only the logo, the Controls pill and the project name, so it
         gets a tighter band than the tool rows below it. */
      QToolBar#headerToolbar { padding: 1px 6px; }
      /* Controls collapse pill: an outlined pill, not bare text — the browser's
         #toggle-controls (1px border, 16px radius, 12px label). */
      QToolButton#controlsPill {
        color: %TEXT%; background: transparent; font-size: 12px;
        border: 1px solid %BORDER%; border-radius: 12px; padding: 2px 10px;
      }
      QToolButton#controlsPill:hover { background: %ACCENT_SOFT%; border-color: %ACCENT_RING%; }
      QToolButton#controlsPill:pressed { background: %ACCENT_SOFT2%; }
      QToolButton {
        color: %TEXT%; background: transparent; padding: 5px 7px;
        border: 1px solid transparent; border-radius: 7px;
      }
      QToolButton:hover { background: %ACCENT_SOFT%; border-color: %ACCENT_RING%; }
      QToolButton:pressed { background: %ACCENT_SOFT2%; }
      /* Active toggle → SOLID accent fill with white text, matching the browser's
         .active toolbar buttons (fullscreen while on, incognito while on) rather than
         a soft tint — so an enabled toggle clearly reads as active. */
      QToolButton:checked {
        background: %ACCENT%; border-color: %ACCENT%; color: white;
      }
      QToolButton:checked:hover { background: %ACCENT2%; border-color: %ACCENT2%; color: white; }
      /* Disabled: a filled, low-contrast "inactive" chip (matches QPushButton:disabled)
         so a greyed toolbar button reads clearly. The :checked:disabled override is
         higher-specificity so a disabled-but-checked toggle (e.g. incognito once an
         image is loaded) drops its accent highlight instead of looking still-active. */
      QToolButton:disabled { color: %MUTED%; background: %DISABLED_BG%; border-color: %BORDER%; }
      QToolButton:checked:disabled { color: %MUTED%; background: %DISABLED_BG%; border-color: %BORDER%; }
      QToolButton::menu-indicator { image: none; }
      /* Chat-dock action buttons (send / attach / gear): the standard FILLED
         accent icon-button treatment (browser chat-panel parity); disabled =
         the shared muted chip. The explicit :disabled rule must follow the
         base rule — same specificity class, last-one-wins. */
      QToolButton[chatAccent="true"] { background: %ACCENT%; border-color: %ACCENT%; color: white; }
      QToolButton[chatAccent="true"]:hover { background: %ACCENT2%; border-color: %ACCENT2%; }
      QToolButton[chatAccent="true"]:pressed { background: %ACCENT2%; }
      /* Disabled accent button: a DIMMED member of the accent set, not the generic
         grey chip. The composer's send sits between attach and the gear; the grey
         treatment made it read as broken rather than merely unavailable. */
      QToolButton[chatAccent="true"]:disabled { color: white; background: %ACCENT_DIM%; border-color: %ACCENT_DIM%; }
      /* Toolbar-section buttons carry a SOLID fill, as in the browser: accent for an
         ordinary action, danger for a destructive one, the shared muted chip when
         disabled. Only the Settings cluster stays a bordered ghost (no toolFill). */
      QToolButton[toolFill="accent"] { background: %ACCENT%; border-color: %ACCENT%; color: white; }
      QToolButton[toolFill="accent"]:hover { background: %ACCENT2%; border-color: %ACCENT2%; }
      QToolButton[toolFill="accent"]:pressed { background: %ACCENT2%; }
      QToolButton[toolFill="danger"] { background: %DANGER%; border-color: %DANGER%; color: white; }
      QToolButton[toolFill="danger"]:hover { background: %DANGER2%; border-color: %DANGER2%; }
      QToolButton[toolFill="danger"]:pressed { background: %DANGER2%; }
      QToolButton[toolFill="accent"]:disabled, QToolButton[toolFill="danger"]:disabled {
        color: %MUTED%; background: %DISABLED_BG%; border-color: %BORDER%;
      }
      /* The Draw Start/Stop toggle is an ACCENT TOGGLE, not another filled section
         button (browser #draw-toggle in layout.css): OUTLINED while idle — accent ring,
         accent word and glyph on a neutral face — and accent-FILLED while a session is
         live, with the on-accent white the app's other filled accent controls use (the
         glyph takes the light-accent halo from accentNeedsGlyphShadow, as chatDock's do).
         It opts out of toolFill for exactly this reason: filled in both states, the
         toggle said nothing about which one you were in. */
      QToolButton[drawToggle="idle"] {
        background: transparent; border-color: %ACCENT%; color: %ACCENT%;
      }
      QToolButton[drawToggle="idle"]:hover { background: %ACCENT_SOFT%; border-color: %ACCENT%; }
      QToolButton[drawToggle="idle"]:pressed { background: %ACCENT_SOFT2%; }
      QToolButton[drawToggle="on"] { background: %ACCENT%; border-color: %ACCENT%; color: white; }
      QToolButton[drawToggle="on"]:hover { background: %ACCENT2%; border-color: %ACCENT2%; color: white; }
      QToolButton[drawToggle="on"]:pressed { background: %ACCENT2%; }
      /* Disabled outranks both states (the browser's :not(:disabled) guard): the shared
         muted chip, so a toggle you cannot press never wears the accent. */
      QToolButton[drawToggle="idle"]:disabled, QToolButton[drawToggle="on"]:disabled {
        color: %MUTED%; background: %DISABLED_BG%; border-color: %BORDER%;
      }
      /* Fit-to-window is the one section button that keeps a GHOST box (browser
         #zoom-fit): an outlined, transparent chip with the theme-coloured glyph, because
         an accent-filled square at the end of the ZOOM row reads as a third zoom step
         rather than "fit the whole image". Disabled = the browser's fade: the outline
         stays, the glyph and text go muted (themedIcon supplies the faded pixmap). */
      QToolButton[toolGhost="true"] {
        background: transparent; border: 1px solid %BORDER%; color: %TEXT%;
      }
      QToolButton[toolGhost="true"]:hover { background: %ACCENT_SOFT%; border-color: %ACCENT_RING%; }
      QToolButton[toolGhost="true"]:pressed { background: %ACCENT_SOFT2%; }
      QToolButton[toolGhost="true"]:disabled { color: %MUTED%; background: transparent; border-color: %BORDER%; }

      /* ── Menu bar + menus: rounded accent hover, comfortable padding ── */
      QMenuBar { background: %BG_CONTROLS%; color: %TEXT%; border-bottom: 1px solid %BORDER%; padding: 2px 4px; }
      QMenuBar::item { background: transparent; padding: 5px 11px; border-radius: 6px; }
      QMenuBar::item:selected { background: %ACCENT_SOFT2%; color: %ACCENT2%; }
      QMenuBar::item:pressed { background: %ACCENT%; color: white; }
      QMenu { background: %BG_CONTAINER%; color: %TEXT%; border: 1px solid %BORDER%; border-radius: 8px; padding: 5px; }
      QMenu::item { padding: 6px 26px 6px 24px; border-radius: 6px; margin: 1px 2px; }
      QMenu::item:selected { background: %ACCENT%; color: white; }
      QMenu::item:disabled { color: %MUTED%; }
      QMenu::separator { height: 1px; background: %BORDER%; margin: 5px 10px; }
      QMenu::icon { padding-left: 6px; }
      QMenu::indicator { width: 16px; height: 16px; left: 6px; }

      /* ── Status bar ── */
      QStatusBar { background: %BG_CONTROLS%; color: %TEXT%; border-top: 1px solid %BORDER%; }
      QStatusBar::item { border: 0; }
      QStatusBar QLabel { color: %TEXT%; }
      QLabel { color: %TEXT%; background: transparent; }

      /* ── Push buttons: gradient face, accent lift on hover, gradient primary ── */
      QPushButton {
        background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %BTN_TOP%, stop:1 %BTN_BOT%);
        color: %TEXT%; border: 1px solid %BORDER%; border-radius: 7px;
        padding: 6px 14px; min-height: 18px;
      }
      QPushButton:hover { border-color: %ACCENT%; background: %ACCENT_SOFT%; }
      QPushButton:pressed { background: %ACCENT_SOFT2%; }
      /* Accent CTA — only the affirmative action button (objectName via makeButtonBox). */
      QPushButton#primaryButton {
        background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %ACCENT%, stop:1 %ACCENT2%);
        color: white; border-color: %ACCENT2%;
      }
      QPushButton#primaryButton:hover {
        background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %ACCENT2%, stop:1 %ACCENT2%);
      }
      /* #primaryButton repeated: its id selector outranks QPushButton:disabled, so the disabled face needs its own rule. */
      QPushButton:disabled, QPushButton#primaryButton:disabled { color: %MUTED%; background: %DISABLED_BG%; border-color: %BORDER%; }
      /* Danger button (e.g. the selection panel's Delete Line) — the browser's
         --danger red treatment, tuned per theme. */
      QPushButton#dangerButton {
        background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %DANGER%, stop:1 %DANGER2%);
        color: white; border-color: %DANGER2%;
      }
      QPushButton#dangerButton:hover { background: %DANGER2%; }
      /* …and a DEAD one has to look it: the id selector outranks QPushButton:disabled above,
         so a disabled Clear All kept painting solid red and still read as pressable. */
      QPushButton#dangerButton:disabled {
        background: %DISABLED_BG%; color: %MUTED%; border-color: %BORDER%;
      }
      /* Muted, spaced section caption (selection panel "POINTS"/"MEASUREMENTS"),
         mirroring the browser's uppercased panel headers. */
      QLabel#panelSectionHeader {
        color: %MUTED%; font-weight: bold; font-size: 11px; letter-spacing: 1px;
        padding: 6px 0 2px 0;
      }

      /* ── Text inputs / combos / spinboxes: rounded, accent focus ring ── */
      QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {
        background: %INPUT_BG%; color: %INPUT_TEXT%; border: 1px solid %BORDER%;
        border-radius: 7px; padding: 3px 8px; min-height: 20px;
        selection-background-color: %ACCENT%; selection-color: white;
      }
      QComboBox:hover, QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover { border-color: %ACCENT_RING%; }
      QComboBox:focus, QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus {
        border: 2px solid %ACCENT%; padding: 2px 7px;
      }
      QComboBox::drop-down { border: 0; width: 18px; }
      /* The browser's chevron, as a real image. A CSS border-triangle is a browser trick
         Qt's stylesheet engine does not draw — it came out as a small solid square — and
         styling the sub-control at all stops the native style painting its own arrow. */
      QComboBox::down-arrow { image: url(%CARET%); width: 9px; height: 9px; margin-right: 5px; }
      QComboBox::down-arrow:hover { image: url(%CARET_HOVER%); }
      /* A dead input has to LOOK dead. The rules above paint every combo/field in the live
         input colours, so a disabled one (the image filter or compare with no image loaded)
         was pixel-identical to a working one. The browser drops those to
         --disabled-bg/--disabled-text (button:disabled over .accent-dd-trigger); this is the
         same step, caret included — a QSS sub-control image is never auto-greyed. */
      QComboBox:disabled, QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
        color: %MUTED%; background: %DISABLED_BG%; border-color: %BORDER%;
      }
      QComboBox::down-arrow:disabled { image: url(%CARET_DIM%); }
      QComboBox QAbstractItemView {
        background: %INPUT_BG%; color: %INPUT_TEXT%; border: 1px solid %BORDER%;
        border-radius: 6px; padding: 3px; outline: none;
        selection-background-color: %ACCENT%; selection-color: white;
      }
      QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { width: 16px; border: 0; background: transparent; }

      QCheckBox { color: %TEXT%; spacing: 7px; }
      QRadioButton { color: %TEXT%; spacing: 7px; }
      /* Checkboxes/radios follow the brand accent (matches the browser
         accent-color: var(--accent)): an empty themed box, filled with the
         accent when checked. */
      QCheckBox::indicator, QRadioButton::indicator {
        width: 16px; height: 16px; border: 1px solid %ACCENT%; background: %INPUT_BG%;
      }
      QCheckBox::indicator { border-radius: 4px; }
      QRadioButton::indicator { border-radius: 9px; }
      QCheckBox::indicator:checked {
        background: %ACCENT%; border-color: %ACCENT%; image: url(:/icons/check.png);
      }
      QRadioButton::indicator:checked {
        background: %ACCENT%; border-color: %ACCENT%; image: url(:/icons/radio-dot.png);
      }
      QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: %ACCENT2%; }
      /* f(x,y) formula toggle: an accent PILL (matches the browser's .pill-toggle) — accent
         outline + text when off, accent-filled with white text when on. The tick indicator is
         hidden; the whole chip conveys the state. */
      QCheckBox#formulaPill {
        border: 1px solid %ACCENT%; border-radius: 6px; padding: 4px 10px;
        color: %ACCENT%; font-weight: 600; background: transparent; spacing: 0px;
      }
      QCheckBox#formulaPill::indicator { width: 0px; height: 0px; margin: 0px; border: none; }
      QCheckBox#formulaPill:hover { background: %ACCENT_SOFT%; }
      QCheckBox#formulaPill:checked { background: %ACCENT%; color: white; border-color: %ACCENT%; }
      /* Projects-list row checkboxes: a light, clearly-outlined box so it reads on BOTH a dark
         row and the purple selected row (the default dark-fill box vanished on dark rows). Unified
         look in every state; checked fills with the accent + a tick. */
      QListWidget#projectsList::indicator {
        width: 16px; height: 16px; border-radius: 4px;
        border: 1px solid #b8bcc6; background: #eef0f4;
      }
      QListWidget#projectsList::indicator:hover { border-color: %ACCENT%; }
      QListWidget#projectsList::indicator:checked {
        background: %ACCENT%; border-color: %ACCENT%; image: url(:/icons/check.png);
      }

      /* ── Dock (selection panel) ── */
      QDockWidget { color: %TEXT%; titlebar-close-icon: none; }
      QDockWidget::title {
        background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %CTRL_TOP%, stop:1 %CTRL_BOT%);
        padding: 5px 10px; border-bottom: 1px solid %BORDER%; font-weight: 600;
      }
      /* The panel supplies its OWN title bar + body (plain QWidgets), which QDockWidget::title
         above never reaches — so both let the window backdrop through and the panel read as a
         hole punched in the page rather than a surface. Same surface as the toolbars, which is
         the browser's --bg-coord-panel in both themes. */
      QWidget#selPanelTitle {
        background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %CTRL_TOP%, stop:1 %CTRL_BOT%);
        border-bottom: 1px solid %BORDER%;
      }
      QWidget#selPanelBody { background: %BG_CONTROLS%; }
      QToolButton#panelCollapseBtn {
        background: transparent; border: 1px solid %BORDER%; border-radius: 8px; padding: 0;
      }
      QToolButton#panelCollapseBtn:hover { background: %ACCENT_SOFT%; border-color: %ACCENT_RING%; }

      /* ── Lists / tables ── */
      QListWidget, QTableWidget, QTreeWidget {
        background: %BG_CONTAINER%; color: %TEXT%; border: 1px solid %BORDER%;
        border-radius: 8px; alternate-background-color: %BG_CONTROLS%; outline: none;
      }
      QListWidget::item, QTreeWidget::item { padding: 4px; border-radius: 6px; }
      QListWidget::item:hover, QTreeWidget::item:hover { background: %ACCENT_SOFT%; }
      QListWidget::item:selected, QTableWidget::item:selected, QTreeWidget::item:selected { background: %ACCENT%; color: white; }
      QHeaderView::section {
        background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %ACCENT%, stop:1 %ACCENT2%);
        color: white; border: 0; padding: 5px 6px;
      }
      QTableWidget { gridline-color: %BORDER%; }
      /* Points table (SelectionPanel): roomy cells, no grid clutter, hover tint, and an outline
         (not a fill) for the selected row — the delegate (PointRowDelegate) draws the outline. */
      QTableWidget#pointsTable {
        gridline-color: transparent;
        selection-background-color: transparent;  /* the delegate strokes an outline instead */
        selection-color: %TEXT%;
      }
      QTableWidget#pointsTable::item { padding: 5px 6px; }
      QTableWidget#pointsTable::item:hover { background: %ACCENT_SOFT%; }
      /* Kill BOTH selection fills (row panel via selection-background-color above + the per-item
         fill here, which would otherwise inherit the generic ::item:selected accent) so only the
         delegate's outline shows. */
      QTableWidget#pointsTable::item:selected { background: transparent; color: %TEXT%; }
      QTableWidget#pointsTable::item:selected:hover { background: %ACCENT_SOFT%; }
      QPushButton#pointDelBtn {
        background: transparent; border: none; border-radius: 6px; padding: 2px;
      }
      QPushButton#pointDelBtn:hover { background: %ACCENT_SOFT%; }

      QDialog { background: %BG_CONTAINER%; color: %TEXT%; }
      QGroupBox {
        border: 1px solid %BORDER%; border-radius: 8px; margin-top: 8px; padding-top: 6px;
      }
      QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: %MUTED%; }
      QTabWidget::pane { border: 1px solid %BORDER%; border-radius: 8px; top: -1px; }
      QTabBar::tab {
        background: transparent; color: %MUTED%; padding: 6px 14px;
        border: 0; border-bottom: 2px solid transparent;
      }
      QTabBar::tab:hover { color: %TEXT%; }
      QTabBar::tab:selected { color: %ACCENT2%; border-bottom: 2px solid %ACCENT%; }

      /* ── Scrollbars: slim, rounded, accent-tinted on hover ── */
      QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
      QScrollBar::handle:vertical { background: %BORDER%; border-radius: 5px; min-height: 28px; }
      QScrollBar::handle:vertical:hover { background: %ACCENT_RING%; }
      QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
      QScrollBar::handle:horizontal { background: %BORDER%; border-radius: 5px; min-width: 28px; }
      QScrollBar::handle:horizontal:hover { background: %ACCENT_RING%; }
      QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
      QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

      /* 2px, because Qt counts this box TWICE: QTipLabel sets its own label margin to
         1 + PM_ToolTipLabelFrameWidth, and under a stylesheet that metric IS border+padding.
         The old 1px/4px therefore became a 6px margin on top of a 5px frame and the text sat
         16px in from the accent outline (user report). At 2px the measured inset is 8px at
         the sides and 7px top/bottom — the browser's #app-tooltip padding: 7px 10px. */
      QToolTip {
        background: %BG_CONTROLS%; color: %TEXT%; border: 1px solid %ACCENT_RING%;
        border-radius: 6px; padding: 2px;
      }

      /* ── Searchable combo popup (SearchComboBox, e.g. the page-format picker):
         the desktop rendering of the browser's .accent-dd-menu panel — rounded
         container, a pinned .accent-dd-search input over a hairline divider,
         hover/selected-tinted option rows, and the muted "no match" row. ── */
      QWidget#searchComboPopup {
        background: %BG_CONTAINER%; border: 1px solid %BORDER%; border-radius: 8px;
      }
      QWidget#searchComboSearchRow {
        background: transparent; border: 0; border-bottom: 1px solid %BORDER%;
      }
      QLineEdit#searchComboSearch {
        background: %INPUT_BG%; color: %INPUT_TEXT%; border: 1px solid %BORDER%;
        border-radius: 6px; padding: 4px 8px; font-size: 13px;
      }
      QLineEdit#searchComboSearch:focus { border: 1px solid %ACCENT%; }
      QListView#searchComboList {
        background: transparent; border: 0; outline: none;
      }
      QListView#searchComboList::item {
        color: %TEXT%; padding: 6px 9px; border-radius: 5px;
      }
      QListView#searchComboList::item:hover { background: %ACCENT_SOFT%; }
      QListView#searchComboList::item:selected {
        background: %ACCENT_SOFT2%; color: %TEXT%;
      }
      QLabel#searchComboNoMatch { color: %MUTED%; padding: 6px 9px; }
    )")
        .replace("%CTRL_TOP%", c(ctrlTop))
        .replace("%CTRL_BOT%", c(ctrlBot))
        .replace("%BTN_TOP%", c(btnTop))
        .replace("%BTN_BOT%", c(btnBot))
        // Dimmed accent fill for disabled accent buttons — still obviously part
        // of the accent group, just muted (see QToolButton[chatAccent]:disabled).
        .replace("%ACCENT_DIM%", rgba(p.accent, dark ? 0.38 : 0.30))
        .replace("%ACCENT_SOFT2%", rgba(p.accent, dark ? 0.30 : 0.20))
        .replace("%ACCENT_SOFT%", rgba(p.accent, dark ? 0.18 : 0.11))
        .replace("%ACCENT_RING%", rgba(p.accent, 0.45))
        .replace("%ACCENT2%", c(accent2))
        // Status reds mirror browser/css/theme.css --danger/--danger-2 (per theme).
        .replace("%DANGER2%", dark ? QStringLiteral("#e8455a") : QStringLiteral("#b71d30"))
        .replace("%DANGER%", dark ? QStringLiteral("#f0697a") : QStringLiteral("#d6293e"))
        .replace("%DISABLED_BG%", c(disabledBg))
        .replace("%BG_PAGE%", c(p.bgPage))
        .replace("%BG_CONTAINER%", c(p.bgContainer))
        .replace("%BG_CONTROLS%", c(p.bgControls))
        .replace("%BORDER%", c(p.borderMain))
        .replace("%TEXT%", c(p.textMain))
        .replace("%CARET_HOVER%", caretImagePath(p.accent))
        .replace("%CARET_DIM%", caretImagePath(caretDim))
        .replace("%CARET%", caretImagePath(p.textMuted))
        .replace("%MUTED%", c(p.textMuted))
        .replace("%ACCENT%", c(p.accent))
        .replace("%INPUT_BG%", c(p.inputBg))
        .replace("%INPUT_TEXT%", c(p.inputText));
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
    q.setColor(QPalette::Disabled, QPalette::Text, p.textMuted);
    q.setColor(QPalette::Disabled, QPalette::ButtonText, p.textMuted);
    q.setColor(QPalette::Disabled, QPalette::WindowText, p.textMuted);
    return q;
  }

}

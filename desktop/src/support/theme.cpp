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
        QColor("#ffc800"), QColor("#7c3aed"), QColor("#e9ecef"),
    };
    static const Palette darkP{
        QColor("#1a1a1a"), QColor("#242424"), QColor("#2d2d2d"),
        QColor("#2e2a17"), QColor("#444444"), QColor("#555555"),
        QColor("#b8860b"), QColor("#e0e0e0"), QColor("#aaaaaa"),
        QColor("#e0b84a"), QColor("#9b6cf2"), QColor("#333333"),
        QColor("#e0e0e0"), QColor("#6d28d9"), QColor("#f0697a"),
        QColor("#ffc800"), QColor("#7c3aed"), QColor("#3a3a3a"),
    };
    Palette p = dark ? darkP : light;
    // Every token goes to the display's space, so reds/golds match the browser too.
    for (QColor* f : {&p.bgPage, &p.bgContainer, &p.bgControls, &p.bgSelPanel, &p.borderMain,
                      &p.borderCanvas, &p.borderSel, &p.textMain, &p.textMuted, &p.textSelLabel,
                      &p.textKey, &p.inputBg, &p.inputText, &p.accent, &p.danger, &p.selGlow,
                      &p.hoverRing, &p.bgCoordHover})
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
    // The dead-control surface (%DISABLED_BG%), and the caret drawn for one: muted pulled
    // halfway to that surface, so a disabled combo's arrow recedes with its text.
    const QColor disabledBg = dark ? p.bgControls : p.bgContainer.darker(108);
    const QColor caretDim = mixSrgb(p.textMuted, disabledBg, 0.55);
    // Canvas scrollbar hover: a lighter/darker shade of the SAME grey, not an accent-colour
    // swap (see the scrollbar QSS comment below).
    const QColor sbThumbHover = dark ? p.borderMain.lighter(135) : p.borderMain.darker(118);
    // Drag & drop hint strip (browser --bg-drop-hint/--border-hint, css/theme.css).
    // NB argument order: CSS color-mix(accent P%, base) == mixSrgb(base, accent, P).
    const QColor dropHintBg = dark ? mixSrgb(p.bgPage, p.accent, 0.16)
                                    : mixSrgb(QColor(Qt::white), p.accent, 0.09);
    const QColor dropHintBorder = dark ? mixSrgb(QColor("#2a2a2a"), p.accent, 0.50)
                                        : mixSrgb(QColor(Qt::white), p.accent, 0.35);
    // The coord-status readout's own border (browser --border-tooltip, css/theme.css) —
    // a distinct, slightly blue-grey hairline neither borderMain nor borderCanvas carries.
    const QColor borderTooltip = displayColor(dark ? QColor("#7f8fa6") : QColor("#2c3e50"));
    // --bg-info (browser theme.css): the neutral info fill — the projects rows' hover
    // wash and the inline-rename editor's chip/input surface.
    const QColor bgInfo = displayColor(dark ? QColor("#2d2d2d") : QColor("#e9ecef"));

    // One stylesheet covering the widgets the app uses. Tracks browser/css —
    // page backdrop, gradient controls, the brand accent, rounded inputs/lists,
    // accent-tinted hovers + focus rings — so the desktop matches the web look.
    return QString(R"(
      QMainWindow, QWidget#centralBackdrop { background: %BG_PAGE%; }
      /* Dock-area resize separators. `width` sizes the VERTICAL bars (left/right dock
         resize — the points panel and a side-docked chat keep their drag handle) and
         `height` the HORIZONTAL ones: 1px, effectively removing the draggable strip
         that sat between the Image Size bar and the canvas (user report) — the docks
         above the canvas are fixed chrome, not something to resize. Backdrop-
         coloured, so the strip never reads as a solid band; the browser-parity
         grip over the canvas ↔ panel separator (a slim bar that tints accent and
         grows under the cursor, ANIMATED — QSS cannot animate) is painted by
         support/dockGrip.hpp, positioned from MainWindow. */
      QMainWindow::separator { background: %BG_PAGE%; width: %SEP_W%px; height: 1px; }

      /* ── Toolbars: gradient surface, hairline divider, rounded icon buttons ── */
      QToolBar {
        background: %BG_CONTROLS%;
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

      /* Image Size bar, right above the canvas — a central-layout row now (see
         MainWindow's centralLayout_), styled like the toolbar rows above it. */
      QWidget#imageInfoBar {
        background: %BG_CONTROLS%;
        border: 0; border-bottom: 1px solid %BORDER%;
      }

      /* Canvas viewport border (browser parity: layout.css .canvas-viewport border: 2px
         solid --border-canvas) — marks off the bg-page gutter around a zoomed-out/blank
         page; the fill itself comes from MainWindow::applyTheme's viewport palette.
         updateProjectTitle() flips [remoteEditing] for a server-backed project. */
      QScrollArea#canvasViewport { border: 2px solid %BORDER_CANVAS%; }
      QScrollArea#canvasViewport[remoteEditing="true"] { border: 2px solid #d4a017; }

      /* Live cursor-coord readout, between the canvas and the drop-hint (browser parity:
         layout.css .coord-status — its own --bg-tooltip/--border-tooltip box, not the
         toolbar's bg-controls). */
      QLabel#coordStatus {
        background: %BG_CONTROLS%; border: 1px solid %BORDER_TOOLTIP%; border-radius: 4px;
        color: %TEXT%; padding: 6px 10px;
        /* Outer gap off the window edge, not inner padding — centralLayout_'s own left
           margin is 0 here (drop-hint shares the row and wants none), so this box needs
           its own small nudge instead. */
        margin-left: 3px;
      }

      /* Drag & drop hint below the canvas (browser .drop-hint parity, mainContent.js):
         a dashed, accent-tinted strip — never the plain window backdrop, or it read as
         just another line of chrome rather than an actual drop target. */
      QWidget#dropHintBar {
        background: %BG_DROP_HINT%; border: 1px dashed %BORDER_HINT%; border-radius: 4px;
      }
      QLabel#dropHintLabel { color: %MUTED%; background: transparent; }

      /* "Selected Line:" bar above the canvas — browser #selection-panel parity: a
         rounded, amber-bordered BOX inset from the window edges (not a flush toolbar
         strip), consistently-sized controls. Docked (selectedLineDock_ —
         Qt::TopDockWidgetArea), not a toolbar: a QDockWidget stretches its one content
         widget (selectedLineBar) to fill the whole dock, so `this` stays a plain, unstyled
         strip and its "selectedLineCard" child (see selectedLineBar.cpp) — inset by
         selectedLineBar's own outer-layout margins — is what actually carries the
         border/radius/background, giving the border room to render on all four sides. */
      QDockWidget#selectedLineDock { border: 0; }
      QWidget#selectedLineBar { background: transparent; border: 0; }
      QWidget#selectedLineCard {
        background: %SEL_BG%; border: 2px solid %SEL_BORDER%; border-radius: 6px;
      }
      QLabel#selectedLineLabel { color: %SEL_LABEL%; font-weight: 700; background: transparent; }
      QLabel#selectedLineFieldLabel { color: %TEXT%; font-weight: 600; background: transparent; }
      QWidget#selectedLineCard QPushButton,
      QWidget#selectedLineCard QComboBox,
      QWidget#selectedLineCard QSpinBox {
        min-height: 20px; padding: 3px 8px; border-radius: 4px;
      }
      QWidget#selectedLineCard QPushButton {
        background: %BG_CONTROLS%; border: 1px solid %BORDER%; color: %TEXT%;
      }
      QWidget#selectedLineCard QPushButton:hover { background: %ACCENT_SOFT%; border-color: %ACCENT%; }
      /* The Deselect CTA needs to outrank the generic button rule above despite sharing
         its specificity class (both are one ID + N type selectors) — CSS/QSS breaks that
         tie by SOURCE ORDER, not by which selector "looks" more specific, so this rule
         must (a) come after the generic one and (b) carry the same ancestor qualifier
         (QWidget#selectedLineCard) or the generic rule's extra type selector actually wins
         instead and the button renders in the plain control style with no accent hover. */
      QWidget#selectedLineCard QPushButton#selectedLineDeselect {
        background: %DESELECT_BG%; color: white; border: none; font-weight: 600;
      }
      QWidget#selectedLineCard QPushButton#selectedLineDeselect:hover { background: %DESELECT_HOVER%; }
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
      /* The Resend/Retry icon under a failed turn: no fill at all on hover, just its
         own muted glyph colour traced as a thin outline (browser .chat-retry-cta /
         extension .chat-retry parity) — the generic accent-soft fill read as a stray
         tinted square on the error card's danger wash (user report). */
      QToolButton#chatRetry:hover { background: transparent; border-color: %MUTED%; }
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
      QMenu::item { padding: 6px %MENU_PAD_R%px 6px 24px; border-radius: 6px; margin: 1px 2px; }
      QMenu::item:selected { background: %BG_COORD_HOVER%; color: %TEXT%; }
      QMenu::item:disabled { color: %MUTED%; }
      QMenu::separator { height: 1px; background: %BORDER%; margin: 5px 10px; }
      /* The project-colour menu (browser .project-menu-item: gap 8px, padding 6px 8px):
         two short rows — the app-wide 24px/26px item padding read as a huge icon gap
         and a slab of dead space right of the text there. */
      QMenu#projectColorMenu::item { padding: 6px 12px 6px 6px; }
      QMenu::icon { padding-left: 6px; }
      QMenu::indicator { width: 16px; height: 16px; left: 6px; }

      /* ── Status bar ── */
      QStatusBar { background: %BG_CONTROLS%; color: %TEXT%; border-top: 1px solid %BORDER%; }
      QStatusBar::item { border: 0; }
      QStatusBar QLabel { color: %TEXT%; }
      QLabel { color: %TEXT%; background: transparent; }

      /* ── Push buttons: gradient face, accent lift on hover, gradient primary ── */
      QPushButton {
        background: %BTN_FLAT%;
        color: %TEXT%; border: 1px solid %BORDER%; border-radius: 7px;
        padding: 6px 14px; min-height: 18px;
      }
      QPushButton:hover { border-color: %ACCENT%; background: %ACCENT_SOFT%; }
      QPushButton:pressed { background: %ACCENT_SOFT2%; }
      /* Accent CTA — the affirmative action button (makeModalCta/makeButtonBox) and the
         chat error cards' own CTAs (Configure provider / Reconnect to <host>): browser
         parity — a plain <button> there is accent-filled by default (layout.css). One
         dynamic-property selector, so the buttons' objectNames stay free for the tests. */
      QPushButton[accentCta="true"] {
        background: %ACCENT%; color: white; border: none;
      }
      QPushButton[accentCta="true"]:hover {
        background: %ACCENT2%;
      }
      /* The accent variant repeated: its selector ties QPushButton:disabled, so the disabled face needs its own rule. */
      QPushButton:disabled, QPushButton[accentCta="true"]:disabled {
        color: %MUTED%; background: %DISABLED_BG%; border-color: %BORDER%;
      }
      /* Danger button (e.g. the selection panel's Delete Line) — the browser's
         --danger red treatment, tuned per theme. */
      QPushButton#dangerButton {
        background: %DANGER%; color: white; border: none;
      }
      QPushButton#dangerButton:hover { background: %DANGER2%; }
      /* …and a DEAD one has to look it: the id selector outranks QPushButton:disabled above,
         so a disabled Clear All kept painting solid red and still read as pressable. */
      QPushButton#dangerButton:disabled {
        background: %DISABLED_BG%; color: %MUTED%; border-color: %BORDER%;
      }
      /* Muted, spaced section caption (selection panel "POINTS"/"MEASUREMENTS", and
         the context menu's own — mainWindowActions.cpp's makeSectionLabel), mirroring
         the browser's uppercased .ctx-sub-label. No padding of its own: every caller
         hosts it in a row whose OWN layout margins already place it (padding here
         would double up with those). */
      QLabel#panelSectionHeader {
        color: %MUTED%; font-weight: bold; font-size: 11px; letter-spacing: 1px;
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
        border: 1px solid %ACCENT%; background: %INPUT_BG%;
      }
      QListWidget#projectsList::indicator:hover { border-color: %ACCENT%; }
      QListWidget#projectsList::indicator:checked {
        background: %ACCENT%; border-color: %ACCENT%; image: url(:/icons/check.png);
      }

      /* ── Dock (selection panel) ── */
      QDockWidget { color: %TEXT%; titlebar-close-icon: none; }
      QDockWidget::title {
        background: %BG_CONTROLS%;
        padding: 5px 10px; border-bottom: 1px solid %BORDER%; font-weight: 600;
      }
      /* The panel supplies its OWN title bar + body (plain QWidgets), which QDockWidget::title
         above never reaches — so both let the window backdrop through and the panel read as a
         hole punched in the page rather than a surface. Same surface as the toolbars, which is
         the browser's --bg-coord-panel in both themes. */
      QWidget#selPanelTitle {
        background: %BG_CONTROLS%;
        border-bottom: 1px solid %BORDER%;
      }
      QWidget#selPanelBody { background: %BG_CONTROLS%; }
      QToolButton#panelCollapseBtn {
        background: transparent; border: 1px solid %BORDER%; border-radius: 8px; padding: 0;
      }
      QToolButton#panelCollapseBtn:hover { background: %ACCENT_SOFT%; border-color: %ACCENT_RING%; }
      /* The Points | Lines strip sits in the panel HEADER, on that surface rather than the
         body's — otherwise it painted its own container colour as a band across the header
         (browser .coord-panel-header holds the tabs and the toggle and nothing else). */
      QTabBar#selectionTabBar { background: transparent; }
      /* …and the pages below it carry no frame of their own: the browser's table IS the
         box (its own hairline border), so a QTabWidget pane around it read as a second,
         rounded container nothing in the browser has. */
      QTabWidget#selectionTabs::pane { border: 0; border-radius: 0; top: 0; }
      /* Open-image dialog tabs (browser .oi-tabs): an underlined strip over plain
         rows — the .vs-rows carry their own hairlines, so no pane box here either.
         Only the strip's own full-width hairline survives, as the pane's top border
         (the tab bar spans just its tabs; the browser line runs the whole body).
         top: -1px tucks that border under the tab bar's last pixel row, so the
         active tab's accent underline OVERLAPS it exactly as the browser's
         margin-bottom: -1px does — instead of a grey line under the accent one.
         The tabs themselves are painted by support/underlineTabBar.hpp, not QSS. */
      QTabWidget#oiTabs::pane { border: 0; border-radius: 0; border-top: 1px solid %BORDER%; top: -1px; }

      /* ── Lists / tables ── */
      QListWidget, QTableWidget, QTreeWidget {
        background: %BG_CONTAINER%; color: %TEXT%; border: 1px solid %BORDER%;
        border-radius: 8px; alternate-background-color: %BG_CONTROLS%; outline: none;
      }
      QListWidget::item, QTreeWidget::item { padding: 4px; border-radius: 6px; }
      QListWidget::item:hover, QTreeWidget::item:hover { background: %ACCENT_SOFT%; }
      QListWidget::item:selected, QTableWidget::item:selected, QTreeWidget::item:selected { background: %ACCENT%; color: white; }
      /* Projects/Connect rows paint their own text and badges in fixed colours (not
         HighlightedText), so the generic solid selected-fill above swallowed them —
         same fix as #pointsTable below: kill it, keep only the hover wash. */
      QListWidget#projectsList::item:selected, QListWidget#connList::item:selected {
        background: transparent; color: %TEXT%;
      }
      QListWidget#projectsList::item:selected:hover, QListWidget#connList::item:selected:hover {
        background: %ACCENT_SOFT%;
      }
      /* Projects rows are CARDS (browser .project-row: input-bg fill, 8px radius,
         8px 10px padding, neutral --bg-info hover — not the accent wash) and the list
         itself is frameless: the browser's list area is just a padded scroll body. */
      QListWidget#projectsList { border: none; background: transparent; }
      /* 3px tighter than the browser's 8px 10px (user decision): Qt's item metrics
         already add their own slack, so the equal padding read wider here. */
      QListWidget#projectsList::item {
        background: %INPUT_BG%; border-radius: 8px; padding: 5px 7px;
      }
      QListWidget#projectsList::item:selected { background: %INPUT_BG%; color: %TEXT%; }
      QListWidget#projectsList::item:hover,
      QListWidget#projectsList::item:selected:hover { background: %BG_INFO%; }
      /* Flat fill, no gradient — matches the browser's own coordinates-table th
         (layout.css: background: var(--bg-coord-th), a single solid colour). */
      QHeaderView::section {
        background: %ACCENT%;
        color: white; border: 0; padding: 5px 6px;
      }
      QTableWidget { gridline-color: %BORDER%; }
      /* Points table (SelectionPanel): roomy cells, no grid clutter, hover tint, and an outline
         (not a fill) for the selected row — the delegate (PointRowDelegate) draws the outline. */
      /* A hairline around every cell, header included — the browser draws one on each th
         and td (layout.css .coordinates-table: border 1px solid --border-coord), which is
         what makes its accent header read as five separate cells rather than one bar. */
      QTableWidget#pointsTable {
        gridline-color: %BORDER%;
        selection-background-color: transparent;  /* the delegate strokes an outline instead */
        selection-color: %TEXT%;
        font-size: 12px;
      }
      QTableWidget#pointsTable QHeaderView::section {
        border-right: 1px solid %BORDER%; border-bottom: 1px solid %BORDER%;
        padding: 6px 5px; font-weight: bold;
      }
      /* The browser's own cell padding (layout.css .coordinates-table: 6px 5px). */
      QTableWidget#pointsTable::item { padding: 6px 5px; }
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
      /* Chrome dialogs are frameless + translucent (modalChrome.cpp): the dialog face
         is see-through and the #modalShell card carries the browser modal's rounded,
         hairline-bordered surface (.app-modal: radius 10px on --bg-container). */
      QDialog[modalChrome="true"] { background: transparent; }
      QWidget#modalShell {
        background: %BG_CONTAINER%; border: 1px solid %BORDER%; border-radius: 10px;
      }
      QWidget#modalHeader { background: transparent; }

      /* ── Browser-modal chrome (support/modalChrome.hpp) — the .settings-header /
         .vs-section / .settings-footer shell every app modal wears in the browser:
         16px bold title beside its glyph, an outlined "✕ Close" pill, full-bleed
         hairline dividers, tracked uppercase section captions, and a muted footer
         hint. The pill deliberately keeps the browser's rounded-full 12px radius
         and quiet transparent face rather than the app's gradient button. ── */
      QLabel#modalTitle { font-size: 16px; font-weight: bold; background: transparent; }
      QPushButton#modalClosePill {
        background: transparent; border: 1px solid %BORDER%; border-radius: 13px;
        color: %TEXT%; font-size: 13px; padding: 4px 12px; min-height: 0px;
      }
      QPushButton#modalClosePill:hover { background: %ACCENT_SOFT%; border-color: %ACCENT_RING%; }
      QPushButton#modalClosePill:pressed { background: %ACCENT_SOFT2%; }
      QFrame#modalDivider {
        background: %BORDER%; border: none; min-height: 1px; max-height: 1px;
      }
      QLabel#modalSection {
        color: %MUTED%; font-weight: bold; font-size: 12px; letter-spacing: 1px;
        background: transparent;
      }
      /* 11px, not the browser's 12: Qt's system font tracks a touch wider, and at the
         shared 560px modal width the connect hint has to land on ONE line as it does
         in the browser. */
      QLabel#modalFooterHint { color: %MUTED%; font-size: 11px; background: transparent; }
      /* Assistant modal extras (llmSettingsForm.cpp): the tinted help note
         (browser .chat-cors-note — accent-tinted fill, solid hint border, muted
         12px type) and the label-less status row's muted text
         (.chat-server-status). */
      QFrame#llmNoteBox {
        background: %BG_DROP_HINT%; border: 1px solid %BORDER_HINT%; border-radius: 6px;
      }
      QFrame#llmNoteBox QLabel { color: %MUTED%; font-size: 12px; background: transparent; }
      QLabel#llmStatus { color: %MUTED%; font-size: 12px; background: transparent; }
      /* Inline project-rename editor (browser .project-name-edit + .name-edit-btn):
         a bold input on the info fill, accent-rimmed while focused, with the ✓/✗ as
         small outlined chips — their glyphs carry the green/red. */
      QWidget#projectsRenameBox { background: transparent; }
      QLineEdit#projectsRenameEdit {
        background: %BG_INFO%; color: %TEXT%; border: 1px solid %BORDER%;
        border-radius: 4px; padding: 1px 6px; font-weight: 600;
      }
      QLineEdit#projectsRenameEdit:focus { border-color: %ACCENT%; }
      QToolButton#projectsRenameBtn {
        background: %BG_INFO%; border: 1px solid %BORDER%; border-radius: 5px;
        padding: 2px 7px;
      }
      QToolButton#projectsRenameBtn:hover { background: %ACCENT_SOFT%; border-color: %ACCENT_RING%; }
      /* Browser .vs-row form rows (open-image dialog): a hairline under each row and a
         plain-weight 13px label column (components.css .vs-row / its label). */
      QWidget[vsRow="true"] { background: transparent; border-bottom: 1px solid %BORDER%; }
      QLabel[vsLabel="true"] { background: transparent; font-size: 13px; }
      /* Browser .bi-preset blank-fill swatch buttons: real white/black chips. */
      QPushButton#biPresetWhite, QPushButton#biPresetBlack {
        min-width: 54px; max-width: 54px; min-height: 28px; max-height: 28px;
        padding: 0px; font-size: 11px; border: 1px solid %BORDER%; border-radius: 4px;
      }
      QPushButton#biPresetWhite { background: #ffffff; color: #333333; }
      QPushButton#biPresetBlack { background: #000000; color: #eeeeee; }
      /* Compact icon-only row chips (the links ↗/✕): the global button padding
         (6px 14px) leaves a 34px chip almost no content box, clipping the glyph —
         these centre their icon in the whole chip instead. */
      QPushButton[miniChip="true"] { padding: 2px; min-height: 0px; }
      /* The browser's danger row chips (.links-clear.danger): a solid danger fill
         with the white glyph — never a washed-out "inactive" tint. */
      QPushButton[modalDangerGhost="true"] {
        background: %DANGER%; border: none; border-radius: 6px;
      }
      QPushButton[modalDangerGhost="true"]:hover { background: %DANGER2%; }
      QPushButton[modalDangerGhost="true"]:pressed { background: %DANGER2%; }
      QGroupBox {
        border: 1px solid %BORDER%; border-radius: 8px; margin-top: 8px; padding-top: 6px;
      }
      QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: %MUTED%; }
      QTabWidget::pane { border: 1px solid %BORDER%; border-radius: 8px; top: -1px; }
      /* Left-aligned tab strip (browser parity) — the macOS style centres it otherwise. */
      QTabWidget::tab-bar { alignment: left; left: 0; }
      QTabBar::tab {
        background: transparent; color: %MUTED%; padding: 6px 14px;
        border: 0; border-bottom: 2px solid transparent;
      }
      QTabBar::tab:hover { color: %TEXT%; }
      QTabBar::tab:selected { color: %ACCENT2%; border-bottom: 2px solid %ACCENT%; }

      /* ── Scrollbars: only the HANDLE takes colour, the track stays transparent. Use
         `::handle:hover` (state on the sub-control itself) — `QScrollBar:hover::handle`
         isn't a form Qt's QSS engine supports and paints the WHOLE groove solid instead.
         Hover is a lighter/darker shade of the same grey (browser parity: --sb-thumb-hover),
         not an accent swap. Bar visibility (hidden until an actual pan/zoom) is handled in
         code, not QSS — see MainWindow::revealCanvasScrollbars. */
      QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
      QScrollBar::handle:vertical { background: %BORDER%; border-radius: 5px; min-height: 28px; }
      QScrollBar::handle:vertical:hover { background: %SB_THUMB_HOVER%; }
      QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
      QScrollBar::handle:horizontal { background: %BORDER%; border-radius: 5px; min-width: 28px; }
      QScrollBar::handle:horizontal:hover { background: %SB_THUMB_HOVER%; }
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
      /* …and the FADING twin that replaces it on controls (support/appTooltip.hpp). An
         ordinary QFrame, so it counts its box once — hence the browser's real padding
         here rather than QTipLabel's doubled 2px. */
      QFrame#stencilAppTooltip {
        background: %BG_CONTROLS%; border: 1px solid %ACCENT_RING%;
        border-radius: 6px; padding: 7px 10px;
      }
      QLabel#stencilAppTooltipBody { background: transparent; color: %TEXT%; }

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
        .replace("%BTN_FLAT%", c(dark ? p.bgContainer.lighter(112) : p.bgContainer.darker(103)))
        // Geometry the code measures against (theme.hpp) — interpolated, never retyped.
        .replace("%MENU_PAD_R%", QString::number(kMenuItemRightPadPx))
        .replace("%SEP_W%", QString::number(kDockSeparatorPx))
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
        // "Selected Line:" bar amber, mirroring browser/css/components.css
        // --bg-sel-panel / --border-sel / --text-sel-label (per theme). Routed through
        // the Palette (already display-space converted above), not re-hardcoded here —
        // a raw sRGB literal painted directly by Qt reads over-saturated on a wide-gamut
        // (P3) Mac display, same reason every other palette token goes through displayColor().
        .replace("%SEL_BG%", c(p.bgSelPanel))
        .replace("%SEL_BORDER%", c(p.borderSel))
        .replace("%SEL_LABEL%", c(p.textSelLabel))
        // The Deselect CTA's browser-hardcoded orange (layout.css .deselect-btn, no
        // light/dark variant) — same display-space conversion, or it reads noticeably
        // more saturated/brighter here than the color-managed browser rendering.
        .replace("%DESELECT_BG%", c(displayColor(QColor("#e67e22"))))
        .replace("%DESELECT_HOVER%", c(displayColor(QColor("#ca6f1e"))))
        .replace("%DISABLED_BG%", c(disabledBg))
        .replace("%BG_PAGE%", c(p.bgPage))
        .replace("%BG_CONTAINER%", c(p.bgContainer))
        .replace("%BG_CONTROLS%", c(p.bgControls))
        .replace("%BG_COORD_HOVER%", c(p.bgCoordHover))
        .replace("%BG_INFO%", c(bgInfo))
        .replace("%BG_DROP_HINT%", c(dropHintBg))
        .replace("%BORDER_HINT%", c(dropHintBorder))
        .replace("%BORDER_TOOLTIP%", c(borderTooltip))
        .replace("%BORDER_CANVAS%", c(p.borderCanvas))
        .replace("%BORDER%", c(p.borderMain))
        .replace("%SB_THUMB_HOVER%", c(sbThumbHover))
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

#include "theme.hpp"
#include <QGuiApplication>
#include <QStyleHints>
#ifdef Q_OS_LINUX
#include <QProcess>
#endif

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
  // Qt 6.5+ QStyleHints::colorScheme() is preferred, but on X11/GNOME it often
  // returns Unknown (the xdg-desktop-portal appearance source isn't visible to
  // Qt), so we fall back to the freedesktop portal value and then the GNOME
  // setting directly — fixing the Fedora case where prefer-dark was ignored.
  bool systemPrefersDark() {
#ifdef Q_OS_LINUX
    // GNOME is the source of truth on Linux. NOTE: under xcb on GNOME,
    // QStyleHints::colorScheme() wrongly reports Light (not Unknown), so the app
    // launched light even with prefer-dark. We therefore consult the desktop
    // portal + gsettings FIRST and only trust Qt's hint when neither answers.
    // These probes are Linux-only: on macOS/Windows Qt's hint is reliable, and
    // running gdbus/gsettings there only risks picking up stray PATH binaries
    // (e.g. a Homebrew install) that don't reflect the OS appearance.

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

  // Port of the browser theme toggle's tri-state resolution (S14).
  bool resolveDark(const QString& mode) {
    if (mode == "dark") return true;
    if (mode == "light") return false;
    return systemPrefersDark();  // "system" (default)
  }

  // The selectable brand-accent presets. Keys + hexes mirror the browser
  // (accents.js) and extension (accent.js) data-accent presets; violet is the
  // default. The darker/lighter --accent-2 shade derives from the primary below.
  const std::vector<AccentPreset>& accentPresets() {
    static const std::vector<AccentPreset> presets{
        {"violet", "Violet", "#7c3aed"},   {"pink", "Pink", "#ec4899"},
        {"yellow", "Yellow", "#eab308"},   {"orange", "Orange", "#ea580c"},
        {"crimson", "Crimson", "#be123c"}, {"aqua", "Aqua", "#0891b2"},
        {"sky", "Sky blue", "#0ea5e9"},    {"blue", "Blue", "#2563eb"},
        {"grass", "Grass green", "#16a34a"}, {"green", "Green", "#047857"},
        {"brown", "Brown", "#a87c50"},     {"grey", "Grey", "#64748b"},
    };
    return presets;
  }

  QColor accentPrimary(const QString& accentKey) {
    for (const AccentPreset& a : accentPresets())
      if (a.key == accentKey) return QColor(a.hex);
    // A custom accent stored as a hex string (set via the desktop logo's double-click picker).
    if (accentKey.startsWith('#')) {
      QColor c(accentKey);
      if (c.isValid()) return c;
    }
    return QColor("#7c3aed");  // unknown key -> violet (the default)
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

  // Values copied verbatim from browser/css/theme.css + the DEFAULT_VISUALS
  // block of browser/js/config/constants.json. The accent + textKey fields are
  // then overridden from the chosen accentKey (accent itself, and textKey =
  // --accent-2), so the whole app recolours to the selected brand colour.
  Palette themePalette(bool dark, const QString& accentKey) {
    static const Palette light{
        QColor("#f0f0f0"), QColor("#ffffff"), QColor("#f8f9fa"),
        QColor("#fff8e1"), QColor("#dddddd"), QColor("#dddddd"),
        QColor("#f0b429"), QColor("#000000"), QColor("#888888"),
        QColor("#7a5c00"), QColor("#6d28d9"), QColor("#ffffff"),
        QColor("#000000"), QColor("#7c3aed"), QColor("#ffc800"),
        QColor("#7c3aed"),
    };
    static const Palette darkP{
        QColor("#1a1a1a"), QColor("#242424"), QColor("#2d2d2d"),
        QColor("#2e2a17"), QColor("#444444"), QColor("#555555"),
        QColor("#b8860b"), QColor("#e0e0e0"), QColor("#aaaaaa"),
        QColor("#e0b84a"), QColor("#9b6cf2"), QColor("#333333"),
        QColor("#e0e0e0"), QColor("#6d28d9"), QColor("#ffc800"),
        QColor("#7c3aed"),
    };
    Palette p = dark ? darkP : light;
    const QColor accent = accentPrimary(accentKey);
    p.accent = accent;
    p.textKey = accentShade(accent, dark);
    return p;
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

    // One stylesheet covering the widgets the app uses. Tracks browser/css —
    // page backdrop, gradient controls, the brand accent, rounded inputs/lists,
    // accent-tinted hovers + focus rings — so the desktop matches the web look.
    return QString(R"(
      QMainWindow, QWidget#centralBackdrop { background: %BG_PAGE%; }

      /* ── Toolbars: gradient surface, hairline divider, rounded icon buttons ── */
      QToolBar {
        background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %CTRL_TOP%, stop:1 %CTRL_BOT%);
        border: 0; border-bottom: 1px solid %BORDER%;
        spacing: 3px; padding: 4px 6px;
      }
      QToolBar::separator {
        width: 1px; background: %BORDER%; margin: 5px 6px; border-radius: 1px;
      }
      QToolBar QLabel { color: %MUTED%; background: transparent; padding: 0 2px; }
      QToolButton {
        color: %TEXT%; background: transparent; padding: 5px 7px;
        border: 1px solid transparent; border-radius: 7px;
      }
      QToolButton:hover { background: %ACCENT_SOFT%; border-color: %ACCENT_RING%; }
      QToolButton:pressed { background: %ACCENT_SOFT2%; }
      QToolButton:checked {
        background: %ACCENT_SOFT2%; border-color: %ACCENT%; color: %ACCENT2%;
      }
      /* Disabled: a filled, low-contrast "inactive" chip (matches QPushButton:disabled)
         so a greyed toolbar button reads clearly. The :checked:disabled override is
         higher-specificity so a disabled-but-checked toggle (e.g. incognito once an
         image is loaded) drops its accent highlight instead of looking still-active. */
      QToolButton:disabled { color: %MUTED%; background: %DISABLED_BG%; border-color: %BORDER%; }
      QToolButton:checked:disabled { color: %MUTED%; background: %DISABLED_BG%; border-color: %BORDER%; }
      QToolButton::menu-indicator { image: none; }

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
        padding: 7px 10px; border-bottom: 1px solid %BORDER%; font-weight: 600;
      }

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

      QToolTip {
        background: %BG_CONTROLS%; color: %TEXT%; border: 1px solid %ACCENT_RING%;
        border-radius: 6px; padding: 5px 8px;
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
        .replace("%ACCENT_SOFT2%", rgba(p.accent, dark ? 0.30 : 0.20))
        .replace("%ACCENT_SOFT%", rgba(p.accent, dark ? 0.18 : 0.11))
        .replace("%ACCENT_RING%", rgba(p.accent, 0.45))
        .replace("%ACCENT2%", c(accent2))
        // Status reds mirror browser/css/theme.css --danger/--danger-2 (per theme).
        .replace("%DANGER2%", dark ? QStringLiteral("#e8455a") : QStringLiteral("#b71d30"))
        .replace("%DANGER%", dark ? QStringLiteral("#f0697a") : QStringLiteral("#d6293e"))
        .replace("%DISABLED_BG%", c(dark ? p.bgControls : p.bgContainer.darker(108)))
        .replace("%BG_PAGE%", c(p.bgPage))
        .replace("%BG_CONTAINER%", c(p.bgContainer))
        .replace("%BG_CONTROLS%", c(p.bgControls))
        .replace("%BORDER%", c(p.borderMain))
        .replace("%TEXT%", c(p.textMain))
        .replace("%MUTED%", c(p.textMuted))
        .replace("%ACCENT%", c(p.accent))
        .replace("%INPUT_BG%", c(p.inputBg))
        .replace("%INPUT_TEXT%", c(p.inputText));
  }

  // Build a QPalette from the same theme tokens the stylesheet uses, so menus,
  // popups and other native bits match. Set on qApp in applyTheme() (S14).
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
    q.setColor(QPalette::Disabled, QPalette::Text, p.textMuted);
    q.setColor(QPalette::Disabled, QPalette::ButtonText, p.textMuted);
    q.setColor(QPalette::Disabled, QPalette::WindowText, p.textMuted);
    return q;
  }

}

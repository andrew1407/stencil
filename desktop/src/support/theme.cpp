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

    // One stylesheet covering the widgets the app uses. Kept close to the CSS:
    // page backdrop, light-grey controls, violet accent, themed inputs + lists.
    return QString(R"(
      QMainWindow, QWidget#centralBackdrop { background: %BG_PAGE%; }
      QToolBar { background: %BG_CONTROLS%; border: 0; border-bottom: 1px solid %BORDER%; spacing: 4px; padding: 3px; }
      QMenuBar { background: %BG_CONTROLS%; color: %TEXT%; border-bottom: 1px solid %BORDER%; }
      QMenuBar::item { background: transparent; padding: 4px 10px; }
      QMenuBar::item:selected { background: %ACCENT%; color: white; }
      QMenu { background: %BG_CONTAINER%; color: %TEXT%; border: 1px solid %BORDER%; }
      QMenu::item:selected { background: %ACCENT%; color: white; }
      QMenu::separator { height: 1px; background: %BORDER%; margin: 4px 8px; }
      QStatusBar { background: %BG_CONTROLS%; color: %TEXT%; border-top: 1px solid %BORDER%; }
      QStatusBar QLabel { color: %TEXT%; }
      QLabel { color: %TEXT%; background: transparent; }
      QToolButton { color: %TEXT%; background: transparent; padding: 4px 8px; border-radius: 4px; }
      QToolButton:hover { background: %BORDER%; }
      QToolButton:disabled { color: %MUTED%; }
      QPushButton { background: %BG_CONTAINER%; color: %TEXT%; border: 1px solid %BORDER%; border-radius: 4px; padding: 5px 12px; }
      QPushButton:hover { border-color: %ACCENT%; }
      QPushButton:default { background: %ACCENT%; color: white; border-color: %ACCENT%; }
      QPushButton:disabled { color: %MUTED%; }
      QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {
        background: %INPUT_BG%; color: %INPUT_TEXT%; border: 1px solid %BORDER%;
        border-radius: 4px; padding: 2px 6px; min-height: 20px;
      }
      QComboBox QAbstractItemView { background: %INPUT_BG%; color: %INPUT_TEXT%; selection-background-color: %ACCENT%; }
      QCheckBox { color: %TEXT%; }
      QDockWidget { color: %TEXT%; titlebar-close-icon: none; }
      QDockWidget::title { background: %BG_CONTROLS%; padding: 5px; border-bottom: 1px solid %BORDER%; }
      QListWidget, QTableWidget, QTreeWidget {
        background: %BG_CONTAINER%; color: %TEXT%; border: 1px solid %BORDER%;
        alternate-background-color: %BG_CONTROLS%;
      }
      QListWidget::item:selected, QTableWidget::item:selected { background: %ACCENT%; color: white; }
      QHeaderView::section { background: %ACCENT%; color: white; border: 0; padding: 4px; }
      QDialog { background: %BG_CONTAINER%; color: %TEXT%; }
      QScrollBar:vertical { background: transparent; width: 12px; }
      QScrollBar::handle:vertical { background: %BORDER%; border-radius: 6px; min-height: 24px; }
      QScrollBar:horizontal { background: transparent; height: 12px; }
      QScrollBar::handle:horizontal { background: %BORDER%; border-radius: 6px; min-width: 24px; }
      QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
      QToolTip { background: %BG_CONTROLS%; color: %TEXT%; border: 1px solid %BORDER%; padding: 4px; }
    )")
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
    q.setColor(QPalette::PlaceholderText, p.textMuted);
    q.setColor(QPalette::Disabled, QPalette::Text, p.textMuted);
    q.setColor(QPalette::Disabled, QPalette::ButtonText, p.textMuted);
    q.setColor(QPalette::Disabled, QPalette::WindowText, p.textMuted);
    return q;
  }

}

#pragma once
#include "accentDefaults.hpp"
#include "colorMix.hpp"   // mixSrgb / blendColors, used with everything below
#include <QColor>
#include <QHash>
#include <QPalette>
#include <QString>
#include <utility>
#include <vector>

// Light / dark theming: a direct port of the CSS custom properties in browser/css/theme.css.
namespace stencil::gui {

  // Field names mirror the CSS vars.
  struct Palette {
    QColor bgPage;       // --bg-page          (canvas / window backdrop)
    QColor bgContainer;  // --bg-container
    QColor bgControls;   // --bg-controls      (toolbar / menus)
    QColor bgSelPanel;   // --bg-sel-panel     (selection dock)
    QColor borderMain;   // --border-main
    QColor borderCanvas; // --border-canvas
    QColor borderSel;    // --border-sel
    QColor textMain;     // --text-main
    QColor textMuted;    // --text-muted
    QColor textSelLabel; // --text-sel-label
    QColor bgSelBtn;     // --bg-sel-btn       (the bar's own amber buttons)
    QColor bgSelBtnHov;  // --bg-sel-btn-hov
    QColor textSelBtn;   // --text-sel-btn     (their glyph/label — per theme, see theme.css)
    QColor textKey;      // --text-key         (shortcut keys)
    QColor inputBg;      // --input-bg
    QColor inputText;    // --input-text
    QColor accent;       // --bg-coord-th      (primary / table header — brand violet)
    QColor danger;       // --danger           (errors; lifted on dark, like the CSS)
    QColor selGlow;      // DEFAULT_VISUALS.selGlowColor
    QColor hoverRing;    // DEFAULT_VISUALS.hoverRingColor
    QColor bgCoordHover; // --bg-coord-hover   (neutral row-hover tint, e.g. .ctx-item:hover —
                         // NOT accent-tinted; a fixed light/dark gray the same across accents)
    QColor warning;      // --warning          (a tooltip's disabled-reason line: .tip-note)
    QColor disabledText; // --disabled-text    (a dead control's label AND its glyph)
    // --on-accent: derived in themePalette(), not in the tables below.
    QColor onAccent;
  };

  // Mirrors the browser (accents.js) / extension (logo/accent.js) data-accent presets.
  struct AccentPreset {
    QString key;
    QString label;
    QString hex;
  };

  // Display order; the first (violet) is the default.
  const std::vector<AccentPreset>& accentPresets();

  // Defaults to violet for unknown keys.
  QColor accentPrimary(const QString& accentKey);

  // Mirrors browser accents.js needsDarkGlyph / extension logo/accent.js — all three flip on the same accents.
  bool accentNeedsDarkGlyph(const QColor& accent);

  QColor onAccentInk(const QColor& accent);

  // Named themePalette, not palette, to avoid shadowing QWidget::palette().
  Palette themePalette(bool dark, const QString& accentKey = DEFAULT_ACCENT_KEY);

  // --bg-info: not in Palette, but the connections rows build their own sheet.
  QColor infoBackground(bool dark);

  // --danger-2, for a widget that builds its own sheet.
  QColor dangerHover(bool dark);

  // --accent-2: darker in light mode, lighter in dark, at theme.css's own 86/14 · 78/22 ratios.
  QColor accentShade(const QColor& primary, bool dark);

  // QSS geometry the code also measures against — interpolated, so the two cannot drift.
  inline constexpr int MENU_ITEM_RIGHT_PAD_PX = 26;   // QMenu::item right padding
  inline constexpr int DOCK_SEPARATOR_PX = 9;       // QMainWindow::separator width
  inline constexpr int CHAT_PAGE_GAP_PX = 10;       // the window's own separator: page between chat and editor
  inline constexpr int FACE_PAD_X_PX = 6;           // QToolButton#drawFaceBtn side padding

  // The app-wide QMenu paddings are sized for the menu bar and read as dead space in flat icon menus.
  inline QString compactMenuQss() {
    return QStringLiteral("QMenu::item{padding:6px 10px 6px 6px;}QMenu::icon{padding-left:4px;}");
  }

  QString buildStylesheet(bool dark, const QString& accentKey = DEFAULT_ACCENT_KEY);

  // One walk over a %TOKEN% template; an unknown token is left alone. A skin's overlay fills its own.
  QString fillStylesheetTokens(const QString& tpl, const QHash<QString, QString>& values);

  // Browser --sb-thumb; read by the painted bars, since QSS on macOS will not round a QScrollBar handle.
  QColor canvasScrollThumb(bool dark);
  QColor canvasScrollThumbHover(bool dark, const QString& accentKey = DEFAULT_ACCENT_KEY);

  // Set on qApp so native bits follow the theme — needed on Fedora.
  QPalette buildQPalette(bool dark, const QString& accentKey = DEFAULT_ACCENT_KEY);

  // Qt 6.5+ QStyleHints::colorScheme(); mirrors matchMedia('(prefers-color-scheme:dark)').
  bool systemPrefersDark();

  // "dark"->true, "light"->false, anything else -> the OS preference (browser theme toggle).
  bool resolveDark(const QString& mode);

}

#pragma once
#include "colorMix.hpp"   // mixSrgb / blendColors, used with everything below
#include <QColor>
#include <QPalette>
#include <QString>
#include <utility>
#include <vector>

// Light / dark theming. The palettes are a direct port of the CSS custom
// properties in browser/css/theme.css (:root = light, [data-theme="dark"] =
// dark); buildStylesheet() turns them into a Qt stylesheet so the desktop app
// matches the browser look, and the canvas-specific colors are exposed for the
// QPainter renderer.
namespace stencil::gui {

  // The subset of theme tokens we render with. Field names mirror the CSS vars.
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
    // --on-accent: the ink an accent-BACKED control paints its label and glyph in — a
    // light accent swallows white. Derived in themePalette(), not in the tables below.
    QColor onAccent;
  };

  // A selectable brand-accent preset: a key (stored in Settings.accentColor), a
  // human label (shown in the Settings dropdown) and the primary hex. Mirrors the
  // browser (accents.js) / extension (accent.js) data-accent presets.
  struct AccentPreset {
    QString key;
    QString label;
    QString hex;
  };

  // The accent presets in display order; the first (violet) is the default.
  const std::vector<AccentPreset>& accentPresets();

  // Primary colour for an accent key (defaults to violet for unknown keys). The
  // darker/lighter --accent-2 shade is derived inside themePalette().
  QColor accentPrimary(const QString& accentKey);

  // True when black's WCAG contrast on `accent` beats white's. Mirrors browser
  // accents.js needsDarkGlyph / extension accent.js — all three flip on the same accents.
  bool accentNeedsDarkGlyph(const QColor& accent);

  // The ink to paint ON `accent`: Palette::onAccent, and the answer for a swatch that
  // paints its own colour (the ✓ on an accent-picker row).
  QColor onAccentInk(const QColor& accent);

  // Palette for the given mode + accent. `dark == false` is the browser default
  // (light); `accentKey` defaults to violet (the brand colour). Returned by value
  // so the accent can vary. (Named themePalette, not palette, to avoid shadowing
  // QWidget::palette().)
  Palette themePalette(bool dark, const QString& accentKey = "violet");

  // --bg-info (css/theme.css): the neutral "raised" fill the app's info boxes and row
  // hovers use. Not in Palette (the QSS reaches it as %BG_INFO%), but the connections
  // rows build their own sheet and need the browser's exact value.
  QColor infoBackground(bool dark);

  // --danger-2: the hover shade the QSS spends as %DANGER2%. Same reason as infoBackground
  // — a widget that builds its own sheet needs the value the stylesheet would have used.
  QColor dangerHover(bool dark);

  // The --accent-2 shade: darker in light mode, lighter in dark, at theme.css's own
  // 86/14 · 78/22 ratios.
  QColor accentShade(const QColor& primary, bool dark);

  // QSS geometry the code also measures against — buildStylesheet interpolates these,
  // so the stylesheet and the arithmetic can't drift.
  inline constexpr int kMenuItemRightPadPx = 26;   // QMenu::item right padding
  inline constexpr int kDockSeparatorPx = 9;       // QMainWindow::separator width

  // Compact popup QSS for short, flat icon+label menus (the chat "…"/row menus,
  // MenuHotkeyChips' compact mode): the app-wide QMenu paddings are sized for the
  // menu bar and read as dead space there.
  inline QString compactMenuQss() {
    return QStringLiteral("QMenu::item{padding:6px 10px 6px 6px;}QMenu::icon{padding-left:4px;}");
  }

  // A Qt stylesheet (QSS) styling the whole app for the given mode + accent.
  QString buildStylesheet(bool dark, const QString& accentKey = "violet");

  // Every scrollbar's thumb (browser css/theme.css --sb-thumb) and its hover shade — read
  // by the painted bars (support/pillScrollBars.hpp), since QSS on macOS will not round
  // a QScrollBar handle or light it under the pointer.
  QColor canvasScrollThumb(bool dark);
  QColor canvasScrollThumbHover(bool dark, const QString& accentKey = "violet");

  // A QPalette matching the theme, set on qApp so native bits (menu/toolbar
  // popups, scrollbars) follow the theme alongside the stylesheet — needed on
  // Fedora where the native style otherwise leaves the chrome unthemed (S14).
  QPalette buildQPalette(bool dark, const QString& accentKey = "violet");

  // Does the OS currently prefer a dark scheme? Uses Qt 6.5+ QStyleHints::
  // colorScheme(). Mirrors the browser matchMedia('(prefers-color-scheme:dark)').
  bool systemPrefersDark();

  // Resolve a tri-state theme mode (S14): "dark"->true, "light"->false, anything
  // else (i.e. "system") -> the OS preference. Port of the browser theme toggle's
  // "follow system when no manual override" behavior.
  bool resolveDark(const QString& mode);

}

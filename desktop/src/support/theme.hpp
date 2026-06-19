#pragma once
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
    QColor textKey;      // --text-key         (shortcut keys)
    QColor inputBg;      // --input-bg
    QColor inputText;    // --input-text
    QColor accent;       // --bg-coord-th      (primary / table header — brand violet)
    QColor selGlow;      // DEFAULT_VISUALS.selGlowColor
    QColor hoverRing;    // DEFAULT_VISUALS.hoverRingColor
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

  // Palette for the given mode + accent. `dark == false` is the browser default
  // (light); `accentKey` defaults to violet (the brand colour). Returned by value
  // so the accent can vary. (Named themePalette, not palette, to avoid shadowing
  // QWidget::palette().)
  Palette themePalette(bool dark, const QString& accentKey = "violet");

  // A Qt stylesheet (QSS) styling the whole app for the given mode + accent.
  QString buildStylesheet(bool dark, const QString& accentKey = "violet");

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

#include "theme.hpp"
#include "themeTokens.hpp"
#include "../skinPrefs.hpp"
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

namespace stencil::gui {

  QColor dangerHover(bool dark) { return themeToken("--danger-2", dark); }

  QColor infoBackground(bool dark) { return themeToken("--bg-info", dark); }

  Palette themePalette(bool dark, const QString& accentKey) {
    // A skin hands the hand-painted chrome its own palette (support/skinPrefs.hpp).
    if (const support::PaletteFn hook = support::skinPalette()) return hook(dark);
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
    // NOT theme.css tokens: constants.json DEFAULT_VISUALS, repainted via Settings.
    p.selGlow = displayColor(QColor("#ffc800"));
    p.hoverRing = displayColor(QColor(DEFAULT_ACCENT_HEX));
    // The canon carries only the violet default; the chosen accent is what the app wears.
    const QColor accent = accentPrimary(accentKey);
    p.accent = accent;
    p.textKey = accentShade(accent, dark);
    p.onAccent = onAccentInk(accent);
    return p;
  }

  // Browser --sb-thumb / --sb-thumb-hover: var(--accent).
  QColor canvasScrollThumb(bool dark) { return themeToken("--sb-thumb", dark); }
  QColor canvasScrollThumbHover(bool dark, const QString& accentKey) {
    return themePalette(dark, accentKey).accent;
  }

  // The same tokens as the stylesheet, so native bits match. Set on qApp in applyTheme().
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
    // Link = accent-2 (browser --text-key).
    q.setColor(QPalette::Link, p.textKey);
    q.setColor(QPalette::PlaceholderText, p.textMuted);
    // Mid = the muted text, read by every `palette(mid)` sheet; Qt's derived grey vanishes on dark.
    q.setColor(QPalette::Mid, p.textMuted);
    // Dark = browser --border-main, the hairline hand-painted card outlines want.
    q.setColor(QPalette::Dark, p.borderMain);
    // Disabled = browser --disabled-text, what a rasterised disabled glyph is inked with (iconSet.cpp mutedInk).
    q.setColor(QPalette::Disabled, QPalette::Text, p.disabledText);
    q.setColor(QPalette::Disabled, QPalette::ButtonText, p.disabledText);
    q.setColor(QPalette::Disabled, QPalette::WindowText, p.disabledText);
    return q;
  }
}


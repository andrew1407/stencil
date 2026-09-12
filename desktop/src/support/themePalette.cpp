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

namespace stencil::gui {

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
    const QColor accent = accentPrimary(accentKey);
    p.accent = accent;
    p.textKey = accentShade(accent, dark);
    p.onAccent = onAccentInk(accent);
    return p;
  }

  // The browser's own thumb grey (css/theme.css --sb-thumb); under the pointer it takes
  // the theme's accent, as the browser's --sb-thumb-hover: var(--accent) does.
  QColor canvasScrollThumb(bool dark) { return themeToken("--sb-thumb", dark); }
  QColor canvasScrollThumbHover(bool dark, const QString& accentKey) {
    return themePalette(dark, accentKey).accent;
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


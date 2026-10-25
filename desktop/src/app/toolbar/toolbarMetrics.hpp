#pragma once

// The toolbar's glyph, row and field sizes, and the centring of a pinned face's label.

#include "../../support/sheets/windowSheets.hpp"
#include "../../support/theme/faceSwap.hpp"

#include <QByteArray>
#include <QString>
#include <QToolButton>

namespace stencil::gui {

  inline constexpr int TOOL_ICON = 18;   // toolbar glyph box
  // Qt's text-beside-icon gap is a fixed 4px and QSS `spacing` does nothing for a QToolButton, so the air is
  // made in the icon RECT (browser twin: .btn-icon-text `gap: 6px`).
  inline constexpr int FACE_ICON_GAP = 3;
  // Qt left-anchors a text-beside-icon face, so the narrower word of a pinned pair takes half its
  // slack (pinned width less its own hint) as left padding (browser .btn-draw-fixed centring).
  inline QByteArray faceHintKey(const QString& label) { return "faceHint_" + label.toUtf8(); }
  inline void centreFaceLabel(QToolButton* btn, int padX, const QString& label) {
    if (!btn || btn->maximumWidth() == QWIDGETSIZE_MAX) return;
    const int hint = btn->property(faceHintKey(label).constData()).toInt();
    const int slack = hint > 0 ? btn->maximumWidth() - hint : 0;
    setFaceBaseSheet(btn, slack > 1 ? support::faceLeftPadSheet(padX + slack / 2) : QString());
  }

  // One size for all four, so edit mode never resizes the row.
  inline constexpr int TOOL_ROW_H = 33;   // icon-button height; every section row matches it
  // The "Controls" pill's chevron is sized against its label, not the toolbar icons.
  inline constexpr int PILL_CHEVRON = 7;    // Controls show/hide arrow (1.5x smaller, user decision)
  inline constexpr int HEADER_LOGO = 33;   // header-row logo mark (user decision); sets the row height floor
  // The browser's inline width (#formula-x / #formula-y); shrinks toward the floor rather than overflowing into "»".
  inline constexpr int FORMULA_FIELD_W = 180;
  inline constexpr int FORMULA_FIELD_MIN_W = 72;   // still shows a typical "x/2 + 10"
  // centralLayout's RIGHT inset while the panel is docked (browser: the .main-content gap); with the
  // 9px separator the canvas sits 16px off the panel, as the browser's 4 + 8 + 4. Collapsed,
  // updatePanelReopenButton swaps in CANVAS_RIGHT_MARGIN_COLLAPSED.
  inline constexpr int CENTRAL_SIDE_MARGIN = 7;

}  // namespace stencil::gui

#pragma once
// The shortcut table's column widths, keycap HTML and form clock, private to the ShortcutsDialog*.cpp TUs.
#include "modalChrome.hpp"
#include "theme.hpp"
#include "tipContent.hpp"

#include <QKeySequence>
#include <QString>

namespace stencil::gui {

  inline constexpr int SHORTCUTS_WIDTH = 620;   // browser #settings-modal: four columns
  // Keycap-column FLOORS: both grow to their widest chord once every row is built
  // (the browser's max-content columns). Action takes the rest, down to its own floor.
  inline constexpr int COMBO_COL_W = 150;
  inline constexpr int DEFAULT_COL_W = 110;
  inline constexpr int ACTION_MIN_W = 150;
  inline constexpr int RESET_COL_W = 30;
  inline constexpr int CELL_PAD_X = 10;   // th/td padding: 7px 10px
  inline constexpr int CELL_PAD_Y = 7;
  inline constexpr int SIDE_PAD = 18;    // .settings-body padding
  // A new combination's caps arrive as dust (browser markIn: 320ms, veiled to 62%).
  inline constexpr int FORM_MS = 320;
  inline constexpr double FORM_VEIL = 0.62;
  inline constexpr int FORM_CELLS = 600;
  // Cap size against the tooltip's own — the combo and its default wear the same caps.
  inline constexpr qreal CAP_SCALE = 0.95;

  // Table keycaps wear no face of their own: the container fill read as a dark box
  // against a hovered row, so here a key is its outline and its glyph.
  inline Palette tableCaps() {
    Palette pal = currentPalette();
    pal.bgContainer = QColor(0, 0, 0, 0);
    return pal;
  }

  inline QString portable(const QKeySequence& k) { return k.toString(QKeySequence::PortableText); }
  // The muted mono line a cell shows with no caps to draw (browser .hotkey-unset).
  inline QString mutedHtml(const QString& text) {
    return QString("<span style=\"color:%1;font-family:Menlo,Consolas,monospace;font-size:12px;\">%2</span>")
        .arg(currentPalette().textMuted.name(), text.toHtmlEscaped());
  }
  inline QString native(const QString& seq) {
    return QKeySequence(seq).toString(QKeySequence::NativeText);
  }

}  // namespace stencil::gui

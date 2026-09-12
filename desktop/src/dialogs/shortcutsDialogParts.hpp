#pragma once
// The shortcut table's column widths, keycap HTML and form clock, private to the shortcutsDialog*.cpp TUs.
#include "modalChrome.hpp"
#include "theme.hpp"
#include "tipContent.hpp"

#include <QKeySequence>
#include <QString>

namespace stencil::gui {

  inline constexpr int kShortcutsWidth = 620;   // browser #settings-modal: four columns
  // Keycap-column FLOORS: both grow to their widest chord once every row is built
  // (the browser's max-content columns). Action takes the rest, down to its own floor.
  inline constexpr int kComboColW = 150;
  inline constexpr int kDefaultColW = 110;
  inline constexpr int kActionMinW = 150;
  inline constexpr int kResetColW = 30;
  inline constexpr int kCellPadX = 10;   // th/td padding: 7px 10px
  inline constexpr int kCellPadY = 7;
  inline constexpr int kSidePad = 18;    // .settings-body padding
  // A new combination's caps arrive as dust (browser markIn: 320ms, veiled to 62%).
  inline constexpr int kFormMs = 320;
  inline constexpr double kFormVeil = 0.62;
  inline constexpr int kFormCells = 600;
  // Cap size against the tooltip's own — the combo and its default wear the same caps.
  inline constexpr qreal kCapScale = 0.95;

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

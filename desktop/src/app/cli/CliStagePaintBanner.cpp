#include "CliStage.hpp"
#include "CliSkinFx.hpp"
#include "theme.hpp"

#include <QFontMetrics>
#include <QPainter>

// The banner's paint: the core's rows cell by cell on the TUI's two panel tones, the mark in the
// annotation yellow (a skin's hue over it), the wordmark beside its row lit letter by letter while
// a wipe plays, and the press frame kept inside the full logo's box (press.zig).

namespace stencil::gui {

  namespace {
    const QColor MARK_YELLOW(0xff, 0xff, 0x00);
    const QColor PANEL_FILL(0x2b, 0x2f, 0x3a);   // logo/palette.zig panel: the margin around the art
    const QColor FRAME_FILL(0x3a, 0x3f, 0x4b);   // panel_inner: the art frame's own ground
  }  // namespace

  int CliStage::paintBanner(QPainter& p, int x, int y) {
    const int cw = cellW(), ch = cellH();
    const QVector<QVector<BannerCell>> cells = bannerCells(worn, compactFrame, fx->clock());
    // The press frame keeps the full logo's box: the smaller icon shrinks towards its middle.
    const QVector<QVector<BannerCell>> box = compactFrame ? bannerCells(worn, false, nullptr) : cells;
    const int top = (box.size() - cells.size()) / 2;
    int cols = 0;
    for (const auto& row : box) cols = qMax(cols, int(row.size()));
    const QVector<cliwipe::RowSpan> swept = wipeRunning() ? cliwipe::wipeFrame(box.size(), cols, wipeX, wipeReach) : QVector<cliwipe::RowSpan>();
    const int frameNow = fx->clock() ? fx->clock()->frame : 0;
    const bool skinned = worn && worn->paintsCells;
    const QColor markInk = skinned ? inkColor() : worn && worn->markColor.isValid() ? worn->markColor : MARK_YELLOW;
    p.setFont(mono);
    for (int r = 0; r < cells.size(); ++r) {
      int first = -1, last = -1;
      for (int c = 0; c < cells[r].size(); ++c)
        if (cells[r][c].ch != QLatin1Char(' ')) { if (first < 0) first = c; last = c; }
      const int ry = y + (r + top) * ch;
      for (int c = 0; c < cells[r].size(); ++c) {
        const BannerCell& cell = cells[r][c];
        if (!skinned && c >= first && c <= last)   // the outline sits on the panel too, so the box reads on a light theme
          p.fillRect(QRect(x + c * cw, ry, cw, ch), cell.role == BannerRole::PANEL ? PANEL_FILL : FRAME_FILL);
        if (cell.ch == QLatin1Char(' ') || cell.ch.isNull()) continue;
        if (cell.role == BannerRole::ACCENT) p.setPen(accentAt(r + top + 1, c, swept));
        else if (cell.role == BannerRole::MARK) p.setPen(worn && worn->paintsRuns ? cliHueAt(*worn, c, frameNow) : markInk);
        else p.setPen(inkColor());
        p.drawText(QRect(x + c * cw, ry, cell.wide ? 2 * cw : cw, ch), Qt::AlignCenter, QString(cell.ch));
      }
    }
    bannerBox = QRect(x, y, cols * cw, box.size() * ch);
    paintWordmark(p, x + (cols + 3) * cw, y + bannerLabelRow(false) * ch, cols + 3);
    return y + box.size() * ch;
  }

  // `col0` is the wordmark's screen column, three past the box, the one its hues run from.
  void CliStage::paintWordmark(QPainter& p, int x, int y, int col0) {
    const int cw = cellW(), ch = cellH();
    QFont bold = mono;
    bold.setBold(true);
    p.setFont(bold);
    const QFontMetrics fm(bold);
    const QString word = cliWordmark(worn);
    const bool pie = worn && worn->replacesLetters && !worn->movesLogo && worn->mark == QLatin1String("P");
    const bool screen = worn && worn->paintsCells && !worn->title.isEmpty();
    if (screen) p.fillRect(QRect(x - cw / 2, y, fm.horizontalAdvance(word) + cw, ch), worn->titleBg);
    int prev = -1, letter = 0;
    for (int i = 0; i < word.size(); ++i) {
      const QChar ch0 = word.at(i);
      if (ch0 == QLatin1Char(' ')) {
        x += cw;
        continue;
      }
      // A plain wordmark lights one letter at a time, an accented one letter by letter and stays
      // (press.zig drawWordmark); the pie's letters are gold and never swapped.
      QColor ink = inkColor();
      if (screen) ink = worn->titleFg;
      else if (pie) ink = cliGold();
      else if (worn && worn->wordmarkAccented) {
        if (worn->paintsRuns) ink = cliHueAt(*worn, col0 + i, frameNow());
        else ink = wipeRunning() ? (letter <= litLetters ? wipeTo : wipeFrom) : accentInk();
      } else if (wipeRunning()) ink = letter == litLetters ? wipeTo : inkColor();
      QString glyph(ch0);
      const bool picture = worn && worn->replacesLetters && fx->clock() && !pie;
      if (picture) {
        prev = cliPictureAt(*worn, 7, col0 + i, prev, *fx->clock());
        glyph = cliPicture(*worn, prev);
      }
      p.setPen(ink);
      const int adv = picture ? 2 * cw : qMax(cw, fm.horizontalAdvance(glyph));   // a picture takes two cells, as the CLI places it
      p.drawText(QRect(x, y, adv, ch), Qt::AlignLeft | Qt::AlignVCenter, glyph);
      x += adv;
      ++letter;
    }
  }

  int CliStage::frameNow() const { return fx->clock() ? fx->clock()->frame : 0; }

}  // namespace stencil::gui

#include "CliStage.hpp"
#include "CliPeek.hpp"
#include "CliSkinFx.hpp"
#include "consoleGrammar.hpp"
#include "theme.hpp"
#include "../../support/console/TypewriterReveal.hpp"

#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>

// The stage's paint: a monospace cell grid through QPainter only — the banner in the accent with
// the mark in the annotation yellow, the wordmark beside its row, the status line and the
// scrollback as it types in. Every run in the accent goes through paintRun (CliStagePaintRun.cpp).

namespace stencil::gui {

  using support::ConsoleKind;

  namespace {
    constexpr int PAD = 12;
    QString statusPrefix() { return QStringLiteral("image: "); }
  }  // namespace

  int CliStage::cellW() const { return qMax(1, QFontMetrics(mono).horizontalAdvance(QLatin1Char('M'))); }
  int CliStage::cellH() const { return qMax(1, QFontMetrics(mono).height()); }

  QColor CliStage::inkColor() const {
    if (worn && worn->paintsCells) return worn->fg;
    return themePalette(hooks.dark && hooks.dark(), hooks.accentKey ? hooks.accentKey() : QString()).textMain;
  }
  QColor CliStage::paperColor() const {
    if (worn && worn->paintsCells) return worn->bg;
    return themePalette(hooks.dark && hooks.dark(), hooks.accentKey ? hooks.accentKey() : QString()).bgPage;
  }

  // An accent cell's colour: the skin's hue, the wipe's old or new accent by the seam, else the accent.
  QColor CliStage::accentAt(int row, int col, const QVector<cliwipe::RowSpan>& swept) const {
    if (worn && worn->paintsRuns) return cliHueAt(*worn, col, fx->clock() ? fx->clock()->frame : 0);
    if (!wipeRunning()) return accentInk();
    for (const cliwipe::RowSpan& s : swept)
      if (s.row == row && col >= s.c0 && col < s.c1) return wipeTo;
    return wipeFrom;
  }

  int CliStage::paintStatus(QPainter& p, int x, int y) {
    const CliStatus s = hooks.status ? hooks.status() : CliStatus{};
    const QFontMetrics fm(mono);
    p.setFont(mono);
    const int at = s.name.isEmpty() ? -1 : s.text.indexOf(s.name, statusPrefix().size());
    if (at < 0) {
      p.setPen(inkColor());
      p.drawText(QPoint(x, y + fm.ascent()), s.text);
      statusName = QRect();
      return y + cellH();
    }
    const QString head = s.text.left(at), tail = s.text.mid(at + s.name.size());
    p.setPen(inkColor());
    p.drawText(QPoint(x, y + fm.ascent()), head);
    const int nameX = x + fm.horizontalAdvance(head);
    statusName = QRect(nameX, y, fm.horizontalAdvance(s.name), cellH());
    const bool cells = worn && worn->paintsCells;
    p.setPen(cells ? inkColor() : s.color.isValid() ? s.color : themePalette(hooks.dark && hooks.dark()).textMuted);
    p.drawText(QPoint(nameX, y + fm.ascent()), s.name);
    p.setPen(inkColor());
    p.drawText(QPoint(nameX + statusName.width(), y + fm.ascent()), tail);
    return y + cellH();
  }

  void CliStage::paintBody(QPainter& p, const QRect& box) {
    const int ch = cellH(), cw = cellW();
    const int rows = qMax(0, box.height() / ch);
    const QFontMetrics fm(mono);
    p.setFont(mono);
    if (fx->raining()) {
      p.setPen(inkColor());
      for (int r = 0; r < rows && r < fx->rainRows().size(); ++r) p.drawText(QPoint(box.left(), box.top() + r * ch + fm.ascent()), fx->rainRows().at(r));
      return;
    }
    const Palette pal = themePalette(hooks.dark && hooks.dark(), hooks.accentKey ? hooks.accentKey() : QString());
    bodyRowsNow = rows;
    scrollOff = qBound(0, scrollOff, qMax(0, reveal->count() - rows));
    const int end = reveal->count() - scrollOff, first = qMax(0, end - rows);
    const int cols = qMax(1, box.width() / cw);
    bodyFirst = first;
    for (int i = first; i < end; ++i) {
      const int y = box.top() + (i - first) * ch;
      const support::TypewriterReveal::Line& l = reveal->at(i);
      const QString text = reveal->visible(i);
      paintSelected(p, i, box.left(), y, text);
      QFont f = mono;
      f.setBold(l.kind == ConsoleKind::TITLE);
      p.setFont(f);
      const bool cells = worn && worn->paintsCells;
      QColor ink = inkColor();
      int x0 = box.left();
      // The run in the accent, re-dressed in the skin or taking the seam while a wipe plays: a help
      // row's command, an echo's prompt and word, a whole section line (press.zig paintRecolored).
      int head = qMin(l.head, int(text.size()));
      switch (l.kind) {
        case ConsoleKind::SECRET: ink = cliGold(); break;
        case ConsoleKind::MUTED: ink = cells ? inkColor() : pal.textMuted; break;
        case ConsoleKind::SECTION: head = text.size(); break;
        case ConsoleKind::TITLE: {   // the blue screen's title bar, centred over the columns
          x0 += qMax(0, (cols - int(l.text.size())) / 2) * cw;
          const QColor bgT = worn && worn->paintsCells ? worn->titleBg : paintedAccent;
          p.fillRect(QRect(x0, y, fm.horizontalAdvance(text), ch), bgT);
          ink = worn && worn->paintsCells ? worn->titleFg : paperColor();
          break;
        }
        case ConsoleKind::ERROR: case ConsoleKind::ECHO: case ConsoleKind::PLAIN: break;
      }
      int taken = head;
      if (head > 0 && l.kind == ConsoleKind::ERROR) {   // `error: ` bold red, the message plain (severity.zig)
        f.setBold(true); p.setFont(f); p.setPen(cells ? inkColor() : pal.danger);
        p.drawText(QPoint(x0, y + fm.ascent()), text.left(head));
        f.setBold(false); p.setFont(f);
      } else if (head > 0) {
        taken = paintRun(p, x0, y, text.left(head), 0, cliRowKey(l.text), true);
      }
      p.setPen(ink);
      p.drawText(QPoint(x0 + taken * cw, y + fm.ascent()), text.mid(head));
    }
  }

  // The selected cells of line i under its text, in the console window's selection tint.
  void CliStage::paintSelected(QPainter& p, int i, int x, int y, const QString& text) {
    if (!hasSelection()) return;
    const bool forward = selAnchor.line < selEnd.line || (selAnchor.line == selEnd.line && selAnchor.col <= selEnd.col);
    const Cell lo = forward ? selAnchor : selEnd, hi = forward ? selEnd : selAnchor;
    if (i < lo.line || i > hi.line) return;
    const int c0 = i == lo.line ? qMin(lo.col, int(text.size())) : 0;
    const int c1 = i == hi.line ? qMin(hi.col, int(text.size())) : text.size();
    if (c1 <= c0) return;
    QColor tint = themePalette(hooks.dark && hooks.dark(), hooks.accentKey ? hooks.accentKey() : QString()).accent;
    tint.setAlphaF(hooks.dark && hooks.dark() ? 0.30 : 0.20);
    p.fillRect(QRect(x + c0 * cellW(), y, (c1 - c0) * cellW(), cellH()), tint);
  }

  CliStage::Cell CliStage::cellAt(const QPoint& at) const {
    if (reveal->count() == 0) return Cell{};
    const int line = qBound(0, bodyFirst + (at.y() - headerBottom) / cellH(), reveal->count() - 1);
    const int col = qBound(0, (at.x() - PAD + cellW() / 2) / cellW(), int(reveal->at(line).text.size()));
    return Cell{line, col};
  }

  void CliStage::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), paperColor());
    const int ch = cellH();
    int y = paintBanner(p, PAD, PAD);
    y = paintStatus(p, PAD, y + ch / 2);
    if (peekView->isVisible()) { peekView->move(PAD, y + 4); y += peekView->height() + 8; }   // the picture, the text below it
    headerBottom = y + 8;
    // The Zig screen's two rules in the accent, the prompt row centred between them.
    const int gap = 8;
    const int promptY = height() - 2 - gap - ch;
    paintBody(p, QRect(PAD, headerBottom, width() - 2 * PAD, promptY - gap - 4 - headerBottom));
    paintRule(p, PAD, promptY - gap, width() - 2 * PAD);
    paintPrompt(p, PAD, promptY);
    paintRule(p, PAD, height() - 2, width() - 2 * PAD);
  }

}  // namespace stencil::gui

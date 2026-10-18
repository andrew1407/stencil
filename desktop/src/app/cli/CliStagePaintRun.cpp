#include "CliStage.hpp"
#include "CliSkinFx.hpp"
#include "consoleGrammar.hpp"

#include <QFontMetrics>
#include <QPainter>

// The accent as the stage paints it under the worn skin (cli/src/console/render/ansi/restyle.zig):
// every run in the accent — a help head, an echo, a section, the prompt and the word being typed,
// the rules — re-dressed in pictures, a hue a column or the cell painter's fg, else the wipe's seam.

namespace stencil::gui {

  QColor CliStage::accentInk() const { return cliAccentUnder(worn, paintedAccent); }

  int CliStage::paintRun(QPainter& p, int x, int y, const QString& text, int col0, int rowKey, bool letters) {
    const int cw = cellW(), ch = cellH();
    const QFontMetrics fm(p.font());
    const bool runs = worn && worn->paintsRuns;
    const bool pictures = worn && worn->replacesLetters && fx->clock();
    if (!runs && !pictures) {
      const int seam = wipeRunning() ? qBound(0, wipeX - col0, int(text.size())) : int(text.size());
      p.setPen(wipeRunning() ? wipeTo : accentInk());
      p.drawText(QPoint(x, y + fm.ascent()), text.left(seam));
      p.setPen(wipeFrom);
      p.drawText(QPoint(x + seam * cw, y + fm.ascent()), text.mid(seam));
      return text.size();
    }
    int col = col0, prev = -1;
    for (const QChar c : text) {
      if (pictures && letters && cliSwappable(c)) {
        prev = cliPictureAt(*worn, rowKey, col, prev, *fx->clock());
        p.setPen(cliGold());
        p.drawText(QRect(x + (col - col0) * cw, y, 2 * cw, ch), Qt::AlignCenter, cliPicture(*worn, prev));
        col += 2;
        continue;
      }
      p.setPen(runs ? cliHueAt(*worn, col, frameNow()) : cliGold());
      p.drawText(QRect(x + (col - col0) * cw, y, cw, ch), Qt::AlignCenter, QString(c));
      col += 1;
    }
    return col - col0;
  }

  // A rule in the accent: a hue a column under the rainbow, else the new accent running ahead of
  // the seam while a wipe plays.
  void CliStage::paintRule(QPainter& p, int x, int y, int width) {
    const int cw = cellW();
    const int cols = qMax(1, width / cw);
    if (worn && worn->paintsRuns) {
      for (int c = 0; c < cols; ++c) p.fillRect(QRect(x + c * cw, y, cw, 1), cliHueAt(*worn, c, frameNow()));
      return;
    }
    if (!wipeRunning()) {
      p.fillRect(QRect(x, y, width, 1), accentInk());
      return;
    }
    const int reach = cliwipe::ruleReach(cols, wipeX, wipeReach) * width / cols;
    p.fillRect(QRect(x, y, reach, 1), wipeTo);
    p.fillRect(QRect(x + reach, y, width - reach, 1), wipeFrom);
  }

  // The prompt row is the input row: its accent re-dresses, its letters never swap (restyleInput).
  void CliStage::paintPrompt(QPainter& p, int x, int y) {
    const int cw = cellW(), ch = cellH();
    const QFontMetrics fm(mono);
    p.setFont(mono);
    paintRun(p, x, y, QStringLiteral(">"), 0, 0, false);
    // The word being typed wears its colour: the accent for a command, gold for a secret (line_edit/render.zig).
    const QString text = line.text();
    const support::ParsedConsoleLine parsed = support::parseConsoleLine(text);
    const int wordEnd = text.indexOf(QLatin1Char(' ')) < 0 ? text.size() : text.indexOf(QLatin1Char(' '));
    if (parsed.slashed && isSecret(parsed.word)) {
      p.setPen(cliGold());
      p.drawText(QPoint(x + 2 * cw, y + fm.ascent()), text.left(wordEnd));
    } else if (support::resolveConsoleCommand(parsed.word, true)) {
      paintRun(p, x + 2 * cw, y, text.left(wordEnd), 2, 0, false);
    } else {
      p.setPen(inkColor());
      p.drawText(QPoint(x + 2 * cw, y + fm.ascent()), text.left(wordEnd));
    }
    p.setPen(inkColor());
    p.drawText(QPoint(x + (2 + wordEnd) * cw, y + fm.ascent()), text.mid(wordEnd));
    const QRect caret(x + (2 + line.caret()) * cw, y, cw, ch);
    p.fillRect(caret, accentInk());
    if (line.caret() < line.text().size()) {
      p.setPen(paperColor());
      p.drawText(caret, Qt::AlignCenter, QString(line.text().at(line.caret())));
    }
  }

}  // namespace stencil::gui

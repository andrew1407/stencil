#include "ScriptHighlighter.hpp"

#include "../../support/theme/theme.hpp"

#include <QGuiApplication>
#include <QPalette>

#include <QTextBlock>
#include <QTextDocument>

namespace stencil::gui {

  using model::ScriptTokenKind;

  ScriptHighlighter::ScriptHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {
    buildFormats();
  }

  void ScriptHighlighter::buildFormats() {
    // Read the live theme the way the other dialogs do: dark is the window's own lightness.
    const bool dark = QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
    const Palette p = themePalette(dark);

    auto set = [&](ScriptTokenKind kind, const QColor& colour, bool italic = false,
                   bool bold = false) {
      QTextCharFormat f;
      f.setForeground(colour);
      if (italic) f.setFontItalic(true);
      if (bold) f.setFontWeight(QFont::DemiBold);
      formats[static_cast<int>(kind)] = f;
    };

    set(ScriptTokenKind::DIRECTIVE, p.accent, false, true);
    set(ScriptTokenKind::KEYWORD, p.textKey);
    set(ScriptTokenKind::PARAM, p.textKey, true);
    set(ScriptTokenKind::NUMBER, p.textMain);
    set(ScriptTokenKind::UNIT, p.textMuted);
    set(ScriptTokenKind::COLOR, p.textMain);
    // The Qt palette carries no --success, so a quoted name takes the amber label ink:
    // distinct from the accent (directives) and from --text-key (keywords).
    set(ScriptTokenKind::STRING, p.textSelLabel);
    set(ScriptTokenKind::COMMENT, p.textMuted, true);
    set(ScriptTokenKind::PUNCT, p.textMuted);
    set(ScriptTokenKind::IDENT, p.textMain);
    set(ScriptTokenKind::ERROR, p.danger);

    // Browser parity (.stk-error / .stk-warning): an ERROR recolours the text danger AND
    // squiggles it; a WARNING only squiggles, so the token keeps its own meaning.
    errorFormat = QTextCharFormat();
    errorFormat.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    errorFormat.setUnderlineColor(p.danger);
    errorFormat.setForeground(p.danger);
    warningFormat = QTextCharFormat();
    warningFormat.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    warningFormat.setUnderlineColor(p.warning);
  }

  void ScriptHighlighter::restyle() {
    buildFormats();
    rehighlight();   // every format changed, so every block is stale
  }

  void ScriptHighlighter::setProgram(const model::ScriptDoc& program, bool withDiagnostics) {
    QVector<QVector<Span>> next;
    const auto lineAt = [&next](int line) -> QVector<Span>& {
      if (next.size() < line) next.resize(line);
      return next[line - 1];
    };
    for (const model::ScriptToken& t : program.getTokens()) {
      Span s;
      s.col = t.col;
      s.len = t.len;
      s.kind = t.kind;
      lineAt(t.line).push_back(s);
    }
    if (withDiagnostics) {
      for (const model::ScriptDiagnostic& d : program.getDiagnostics()) {
        const int len = d.len > 0 ? d.len : 1;
        const Mark mark = d.isError ? Mark::ERROR : Mark::WARNING;
        QVector<Span>& spans = lineAt(d.line);
        bool marked = false;
        for (Span& s : spans) {
          if (s.col >= d.col + len || d.col >= s.col + qMax(1, s.len)) continue;
          s.mark = mark;
          marked = true;
        }
        if (marked) continue;
        Span s;              // a diagnostic past the end of the line still gets a mark
        s.col = d.col;
        s.len = len;
        s.kind = ScriptTokenKind::ERROR;
        s.mark = mark;
        spans.push_back(s);
      }
      // A bad line reads as bad WHOLE: the squiggle stays on the token the diagnostic names,
      // but every span on that line takes the danger ink (browser .stk-line-error).
      for (const model::ScriptDiagnostic& d : program.getDiagnostics()) {
        if (!d.isError) continue;
        for (Span& s : lineAt(d.line)) s.lineErrored = true;
      }
    }

    /* Only the lines whose spans actually moved are repainted: QSyntaxHighlighter has already
     * re-coloured the edited block from the stale spans, and a full rehighlight() would paint
     * every other line a second time on every keystroke. */
    const QVector<QVector<Span>> was = std::move(byLine);
    byLine = std::move(next);
    QVector<int> dirty;
    for (int i = 0, span = qMax(was.size(), byLine.size()); i < span; ++i)
      if (!(i < was.size() && i < byLine.size() && was.at(i) == byLine.at(i))) dirty.push_back(i);

    QTextDocument* doc = document();
    // Each rehighlightBlock is its own edit block, so past half the document one pass is cheaper.
    if (dirty.size() * 2 >= doc->blockCount()) {
      rehighlight();
      return;
    }
    for (const int i : dirty) {
      const QTextBlock block = doc->findBlockByNumber(i);
      if (block.isValid()) rehighlightBlock(block);
    }
  }

  void ScriptHighlighter::highlightBlock(const QString& text) {
    const int line = currentBlock().blockNumber();
    if (line < 0 || line >= byLine.size()) return;
    for (const Span& s : byLine.at(line)) {
      const int start = s.col - 1;
      if (start < 0 || start >= text.size()) continue;
      const int len = qMin(s.len, text.size() - start);
      if (len <= 0) continue;

      // The lexer tags every @word DIRECTIVE, so the colour would claim a misspelling
      // exists. Only a word the core knows keeps it; the rest read as plain text.
      const bool known =
          s.kind != ScriptTokenKind::DIRECTIVE ||
          model::ScriptDoc::isDirectiveWord(QStringView(text).mid(start + 1, len - 1));
      QTextCharFormat f = formats[static_cast<int>(known ? s.kind : ScriptTokenKind::IDENT)];
      if (s.mark != Mark::NONE) {
        const QTextCharFormat& mark = s.mark == Mark::ERROR ? errorFormat : warningFormat;
        f.setUnderlineStyle(mark.underlineStyle());
        f.setUnderlineColor(mark.underlineColor());
        if (mark.foreground().style() != Qt::NoBrush) f.setForeground(mark.foreground());
      }
      if (s.lineErrored) f.setForeground(errorFormat.foreground());
      setFormat(start, len, f);
    }
  }

}  // namespace stencil::gui

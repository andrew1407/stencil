#include "ScriptHighlighter.hpp"

#include "theme.hpp"

#include <QTextBlock>
#include <QTextDocument>

namespace stencil::dialogs {

  using model::ScriptTokenKind;

  ScriptHighlighter::ScriptHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {
    buildFormats();
  }

  void ScriptHighlighter::buildFormats() {
    const support::Palette p = support::currentPalette();
    formats_.clear();

    auto set = [&](ScriptTokenKind kind, const QColor& colour, bool italic = false,
                   bool bold = false) {
      QTextCharFormat f;
      f.setForeground(colour);
      if (italic) f.setFontItalic(true);
      if (bold) f.setFontWeight(QFont::DemiBold);
      formats_.insert(static_cast<int>(kind), f);
    };

    set(ScriptTokenKind::DIRECTIVE, p.accent, false, true);
    set(ScriptTokenKind::KEYWORD, p.textKey);
    set(ScriptTokenKind::PARAM, p.textKey, true);
    set(ScriptTokenKind::NUMBER, p.textMain);
    set(ScriptTokenKind::UNIT, p.textMuted);
    set(ScriptTokenKind::COLOR, p.textMain);
    set(ScriptTokenKind::STRING, p.success);
    set(ScriptTokenKind::COMMENT, p.textMuted, true);
    set(ScriptTokenKind::PUNCT, p.textMuted);
    set(ScriptTokenKind::IDENT, p.textMain);
    set(ScriptTokenKind::ERROR, p.danger);

    // A diagnostic underlines what is already there rather than recolouring it, so the span
    // keeps its meaning and gains the squiggle.
    errorFormat_ = QTextCharFormat();
    errorFormat_.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    errorFormat_.setUnderlineColor(p.danger);
    warningFormat_ = QTextCharFormat();
    warningFormat_.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    warningFormat_.setUnderlineColor(p.warning);
  }

  void ScriptHighlighter::restyle() {
    buildFormats();
    rehighlight();
  }

  void ScriptHighlighter::setProgram(const model::ScriptProgram& program, bool withDiagnostics) {
    byLine_.clear();
    for (const model::ScriptToken& t : program.tokens()) {
      Span s;
      s.col = t.col;
      s.len = t.len;
      s.kind = t.kind;
      byLine_[t.line].push_back(s);
    }
    if (withDiagnostics) {
      for (const model::ScriptDiagnostic& d : program.diagnostics()) {
        const int len = d.len > 0 ? d.len : 1;
        QVector<Span>& spans = byLine_[d.line];
        bool marked = false;
        for (Span& s : spans) {
          if (s.col >= d.col + len || d.col >= s.col + qMax(1, s.len)) continue;
          (d.error ? s.error : s.warning) = true;
          marked = true;
        }
        if (marked) continue;
        Span s;              // a diagnostic past the end of the line still gets a mark
        s.col = d.col;
        s.len = len;
        s.kind = ScriptTokenKind::ERROR;
        (d.error ? s.error : s.warning) = true;
        spans.push_back(s);
      }
    }
    rehighlight();
  }

  void ScriptHighlighter::highlightBlock(const QString& text) {
    const auto spans = byLine_.value(currentBlock().blockNumber() + 1);
    for (const Span& s : spans) {
      const int start = s.col - 1;
      if (start < 0 || start >= text.size()) continue;
      const int len = qMin(s.len, text.size() - start);
      if (len <= 0) continue;

      QTextCharFormat f = formats_.value(static_cast<int>(s.kind));
      if (s.error) {
        f.setUnderlineStyle(errorFormat_.underlineStyle());
        f.setUnderlineColor(errorFormat_.underlineColor());
      } else if (s.warning) {
        f.setUnderlineStyle(warningFormat_.underlineStyle());
        f.setUnderlineColor(warningFormat_.underlineColor());
      }
      setFormat(start, len, f);
    }
  }

}  // namespace stencil::dialogs

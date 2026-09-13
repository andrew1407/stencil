#pragma once

#include "ScriptProgram.hpp"

#include <QHash>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

namespace stencil::dialogs {

  /* Colours a .stc from the core's own token stream, so the editor and the runner never
   * disagree about what a line means. The document is tokenized as a WHOLE (a template body
   * spans lines), and each block looks its own spans up — never re-lexing per block.
   * Diagnostics are shown only after a run: a half-typed line is not a mistake. */
  class ScriptHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

   public:
    explicit ScriptHighlighter(QTextDocument* document);

    // Replaces the spans and repaints. `diagnostics` is empty until the script is run.
    void setProgram(const model::ScriptProgram& program, bool withDiagnostics);

    // Re-reads the palette after a theme flip; formats hold resolved colours.
    void restyle();

   protected:
    void highlightBlock(const QString& text) override;

   private:
    struct Span {
      int col = 1;
      int len = 0;
      model::ScriptTokenKind kind = model::ScriptTokenKind::IDENT;
      bool error = false;
      bool warning = false;
    };

    void buildFormats();

    QHash<int, QVector<Span>> byLine_;   // 1-based line -> its spans
    QHash<int, QTextCharFormat> formats_;
    QTextCharFormat errorFormat_;
    QTextCharFormat warningFormat_;
  };

}  // namespace stencil::dialogs

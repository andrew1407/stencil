#pragma once

#include "ScriptDoc.hpp"

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>
#include <array>

namespace stencil::gui {

  /* Colours a .stc from the core's own token stream, so the editor and the runner never
   * disagree about what a line means. The document is tokenized as a WHOLE (a template body
   * spans lines), and each block looks its own spans up — never re-lexing per block. */
  class ScriptHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

   public:
    explicit ScriptHighlighter(QTextDocument* document);

    // Replaces the spans and repaints only the lines whose spans moved — Qt has already
    // re-coloured the edited block. `diagnostics` is empty until the script is run.
    void setProgram(const model::ScriptDoc& program, bool withDiagnostics);

    // Re-reads the palette after a theme flip; formats hold resolved colours.
    void restyle();

   protected:
    void highlightBlock(const QString& text) override;

   private:
    enum class Mark { NONE, WARNING, ERROR };

    struct Span {
      int col = 1;
      int len = 0;
      model::ScriptTokenKind kind = model::ScriptTokenKind::IDENT;
      Mark mark = Mark::NONE;
      bool lineErrored = false;   // any error on this line reddens every span on it
      bool operator==(const Span& o) const {
        return col == o.col && len == o.len && kind == o.kind && mark == o.mark
               && lineErrored == o.lineErrored;
      }
    };
    static constexpr int KINDS = static_cast<int>(model::ScriptTokenKind::ERROR) + 1;

    void buildFormats();

    QVector<QVector<Span>> byLine_;   // index = 0-based line, so no hash per block
    std::array<QTextCharFormat, KINDS> formats_;
    QTextCharFormat errorFormat_;
    QTextCharFormat warningFormat_;
  };

}  // namespace stencil::gui

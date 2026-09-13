#pragma once

#include <QSize>
#include <QString>
#include <QVector>

// The desktop's seam onto core/script: the ONE file here that includes a core script
// header, so the layer lint's "core only from model/" rule holds. Everything above works
// in Qt types. The language is normative in contracts/stc/stc-contract.md.
namespace stencil::model {

  // Mirrors core::script::TokenKind; the dialog's highlighter colours by this.
  enum class ScriptTokenKind {
    COMMENT, DIRECTIVE, KEYWORD, NUMBER, UNIT, COLOR, STRING, PARAM, PUNCT, IDENT, ERROR,
  };

  struct ScriptToken {
    int line = 1;   // 1-based
    int col = 1;    // 1-based, in bytes
    int len = 0;
    ScriptTokenKind kind = ScriptTokenKind::IDENT;
  };

  struct ScriptDiagnostic {
    bool error = true;   // false = warning
    QString code;        // the stable code, e.g. "E_UNKNOWN_DIRECTIVE"
    int line = 1;
    int col = 1;
    int len = 0;
    QString message;
  };

  enum class ScriptSourceKind { PROJECT, FILE, URL, DIR, GLOB };

  enum class ScriptOpKind { OPEN, FRAME, CROP, FILTER, LINE, RECT, LAYOUT, SAVE, UNDO, REDO };

  /* One lowered operation. `strs` and `nums` are the op's own payload; `resolve()` turns its
   * length tokens into pixels, which is a separate call because a crop changes the image
   * size mid-script. See core/script/scriptTypes.hpp for the per-kind layouts. */
  struct ScriptOp {
    ScriptOpKind kind = ScriptOpKind::CROP;
    int block = 0;
    int line = 1;
    int col = 1;
    int editIndex = 0;
    QVector<QString> strs;
    QVector<QString> toks;
    QVector<double> nums;
  };

  struct ScriptBlock {
    QString source;
    ScriptSourceKind kind = ScriptSourceKind::PROJECT;
    int frame = 0;
    int opStart = 0;
    int opCount = 0;
  };

  /* A parsed .stc. Value type: parse once, read as often as you like. A program with any
   * error must not be executed — the dialog reports instead. */
  class ScriptProgram {
   public:
    static ScriptProgram parse(const QString& text);

    const QVector<ScriptToken>& tokens() const { return tokens_; }
    const QVector<ScriptDiagnostic>& diagnostics() const { return diagnostics_; }
    const QVector<ScriptBlock>& blocks() const { return blocks_; }
    const QVector<ScriptOp>& ops() const { return ops_; }
    bool hasErrors() const;

    /* Length tokens -> pixels against the size the caller holds RIGHT NOW.
     * CROP -> [x, y, w, h]; LINE/RECT -> [x0, y0, …, thickness, pointSize] with a two-point
     * rect expanded to its four corners; FRAME/UNDO/REDO -> [n]. Empty when the op carries
     * no geometry, or when a token cannot be resolved against this size. */
    static QVector<double> resolve(const ScriptOp& op, QSize imageSize);

   private:
    QVector<ScriptToken> tokens_;
    QVector<ScriptDiagnostic> diagnostics_;
    QVector<ScriptBlock> blocks_;
    QVector<ScriptOp> ops_;
  };

}  // namespace stencil::model

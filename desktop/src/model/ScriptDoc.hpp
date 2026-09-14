#pragma once

#include "cropGeometry.hpp"   // core/: this file is the seam that may include it
#include "models.hpp"

#include <QSize>
#include <QString>
#include <QVector>

// The desktop's seam onto core/script: the ONE file here that includes a core script
// header, so the layer lint's "core only from model/" rule holds. Everything above works in
// Qt types, columns included. The language is normative in contracts/stc/stc-contract.md.
namespace stencil::model {

  // Mirrors core::script::TokenKind; the dialog's highlighter colours by this.
  enum class ScriptTokenKind {
    COMMENT, DIRECTIVE, KEYWORD, NUMBER, UNIT, COLOR, STRING, PARAM, PUNCT, IDENT, ERROR,
  };

  // `col` and `len` are 1-based QChar offsets: the core lexes UTF-8 bytes, and parse()
  // converts, so everything above this seam indexes the way QTextDocument does.
  struct ScriptToken {
    int line = 1;
    int col = 1;
    int len = 0;
    ScriptTokenKind kind = ScriptTokenKind::IDENT;
  };

  struct ScriptDiagnostic {
    bool isError = true;   // false = warning
    QString code;          // the stable code, e.g. "E_UNKNOWN_DIRECTIVE"
    int line = 1;
    int col = 1;
    int len = 0;
    QString message;
  };

  enum class ScriptSourceKind { PROJECT, FILE, URL, DIR, GLOB };

  enum class ScriptOpKind { OPEN, FRAME, CROP, FILTER, LINE, RECT, LAYOUT, SAVE, UNDO, REDO };

  /* One lowered operation. `resolve()` is a separate call because a crop changes the image
   * size mid-script. See core/script/scriptTypes.hpp for the per-kind payloads. */
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
   * error must not be executed. */
  class ScriptDoc {
   public:
    static ScriptDoc parse(const QString& text);

    const QVector<ScriptToken>& tokens() const { return tokens_; }
    const QVector<ScriptDiagnostic>& diagnostics() const { return diagnostics_; }
    const QVector<ScriptBlock>& blocks() const { return blocks_; }
    const QVector<ScriptOp>& ops() const { return ops_; }
    bool hasErrors() const;

    /* Length tokens -> pixels against the size the caller holds RIGHT NOW. CROP -> [x, y, w, h];
     * LINE/RECT -> [x0, y0, …, thickness, pointSize], a two-point rect expanded to four corners;
     * FRAME/UNDO/REDO -> [n]. Empty when the op carries no geometry or a token cannot resolve. */
    static QVector<double> resolve(const ScriptOp& op, QSize imageSize);

    /* The two shapes an adapter needs back in the core's own vocabulary. `cropRect` is
     * empty-on-failure, which the caller reports as "resolves to nothing". */
    static core::CropRect cropRect(const ScriptOp& op, QSize imageSize, bool* ok);
    static void appendLine(core::Lines& lines, const ScriptOp& op, QSize imageSize, bool* ok);

   private:
    QVector<ScriptToken> tokens_;
    QVector<ScriptDiagnostic> diagnostics_;
    QVector<ScriptBlock> blocks_;
    QVector<ScriptOp> ops_;
  };

}  // namespace stencil::model

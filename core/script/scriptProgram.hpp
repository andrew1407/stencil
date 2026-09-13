#pragma once
#include "scriptTypes.hpp"

// The parsed script, as the ABI hands it out. Port target: browser/js/core/scriptProgram.js.
namespace stencil::core::script {

  /* Immutable after parse(), which is what lets the C ABI hand out pointers into it that
   * stay valid until the handle is destroyed. */
  class ScriptProgram {
   public:
    static ScriptProgram parse(const char* text, int len);

    const std::vector<Token>& tokens() const { return tokens_; }
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    const std::vector<Block>& blocks() const { return blocks_; }
    const std::vector<Op>& ops() const { return ops_; }
    bool hasErrors() const;
    int errorCount() const;

    // Memoized on first call; the ABI returns a pointer into it.
    const std::string& dump() const;

   private:
    std::vector<Token> tokens_;
    std::vector<Diagnostic> diagnostics_;
    std::vector<Block> blocks_;
    std::vector<Op> ops_;
    mutable std::string dump_;
    mutable bool dumped_ = false;
  };

  /* Resolves one op's length tokens into pixels against the CURRENT image size — crop
   * changes it mid-stream, so the host passes what it holds right now. Writes at most
   * `cap` doubles and returns how many; -1 for an unknown op index, -2 when `cap` is too
   * small. CROP -> [x,y,w,h] · LINE/RECT -> [x0,y0,…,thickness,pointSize] (a two-point
   * rect expands to four corners here) · FRAME/UNDO/REDO -> [n] · others -> 0. */
  int resolveOp(const ScriptProgram& program, int index, double imageW, double imageH,
                double pxPerCmX, double pxPerCmY, double* out, int cap);

}  // namespace stencil::core::script

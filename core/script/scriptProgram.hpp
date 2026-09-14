#pragma once
#include "scriptTypes.hpp"

// The parsed script, as the ABI hands it out. Port target: browser/js/core/scriptProgram.js.
namespace stencil::core::script {

  // Immutable after parse(), so the C ABI can hand out pointers that outlive the call.
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

  /* Resolves one op's length tokens into pixels against the CURRENT image size, which crop
   * changes mid-stream. Writes at most `cap` doubles, returns how many, -1 unresolvable, -2
   * `cap` too small. CROP -> [x,y,w,h] · LINE/RECT -> [x0,y0,…,thickness,pointSize], a
   * two-point rect expanding to four corners · FRAME/UNDO/REDO -> [n] · others -> 0. */
  int resolveOp(const ScriptProgram& program, int index, double imageW, double imageH,
                double pxPerCmX, double pxPerCmY, double* out, int cap);

  // The same for an Op the caller already holds; the program version bounds-checks first.
  int resolveOp(const Op& op, double imageW, double imageH, double pxPerCmX, double pxPerCmY,
                double* out, int cap);

}  // namespace stencil::core::script

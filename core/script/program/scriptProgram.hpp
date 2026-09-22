#pragma once
#include "types.hpp"

// The parsed script as the ABI hands it out: the C++ shape behind what parseScript returns.
namespace stencil::core::script {

  // Immutable after parse(), so the C ABI can hand out pointers that outlive the call.
  class ScriptProgram {
   public:
    static ScriptProgram parse(const char* text, int len);

    const std::vector<Token>& getTokens() const { return tokens; }
    const std::vector<Diagnostic>& getDiagnostics() const { return diagnostics; }
    const std::vector<Block>& getBlocks() const { return blocks; }
    const std::vector<Op>& getOps() const { return ops; }
    bool hasErrors() const;
    int errorCount() const;

    // Memoized on first call; the ABI returns a pointer into it.
    const std::string& getDump() const;

   private:
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;
    std::vector<Block> blocks;
    std::vector<Op> ops;
    mutable std::string dump;
    mutable bool dumped = false;
  };

  // Length tokens -> pixels against the CURRENT image size, which a crop changes mid-stream.
  // Writes at most `cap` doubles, returns how many; -1 unresolvable, -2 `cap` too small.
  int resolveOp(const ScriptProgram& program, int index, double imageW, double imageH,
                double pxPerCmX, double pxPerCmY, double* out, int cap);

  // The same for an Op the caller already holds; the program version bounds-checks first.
  int resolveOp(const Op& op, double imageW, double imageH, double pxPerCmX, double pxPerCmY,
                double* out, int cap);

}  // namespace stencil::core::script

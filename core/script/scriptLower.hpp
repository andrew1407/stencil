#pragma once
#include "scriptParser.hpp"
#include "scriptTypes.hpp"

// Statements -> the flat op stream. Port target: browser/js/core/scriptLower.js.
namespace stencil::core::script {

  struct LowerResult {
    std::vector<Block> blocks;
    std::vector<Op> ops;
    std::vector<Diagnostic> diagnostics;
  };

  // Expands templates, tracks the `@use` state, numbers the edits and resolves
  // @undo/@redo into undo{steps} + replay at every @save and at each block's end.
  LowerResult lowerScript(ParseResult& parsed);

}  // namespace stencil::core::script

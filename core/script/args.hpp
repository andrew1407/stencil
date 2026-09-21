#pragma once
#include "parser.hpp"
#include "types.hpp"

// Per-directive argument grammars. Port target: browser/js/core/script/args.js.
namespace stencil::core::script {

  // Evaluator state a statement can read and change: the default unit and the line style.
  struct EvalState {
    std::string unit = "px";
    LineStyle style;
  };

  // @crop: the key form, or 1/2/4 positional insets.
  bool argsCrop(const Stmt& st, const EvalState& state, Op& op, std::vector<Diagnostic>& diags);

  // @filter <mode|colour> -> strs{mode, tint}.
  bool argsFilter(const Stmt& st, Op& op, std::vector<Diagnostic>& diags);

  // @line / @rect -> the style strings plus the flat point tokens.
  bool argsShape(const Stmt& st, const EvalState& state, bool locked, Op& op,
                 std::vector<Diagnostic>& diags);

  // @layout <path|url> [combine|replace].
  bool argsLayout(const Stmt& st, Op& op, std::vector<Diagnostic>& diags);

  // @save [target] · @frame <n> · @undo [selectors] · @redo [n]
  bool argsSave(const Stmt& st, Op& op, std::vector<Diagnostic>& diags);
  bool argsFrame(const Stmt& st, Op& op, std::vector<Diagnostic>& diags);

  // @use <unit> | @use line <groups> — mutates `state`. `isStencilUse` marks the form the
  // caller expands instead.
  bool argsUse(const Stmt& st, EvalState& state, bool& isStencilUse,
               std::vector<Diagnostic>& diags);

  SourceKind classifySource(const std::string& spec);

}  // namespace stencil::core::script

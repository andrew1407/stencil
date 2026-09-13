#pragma once
#include "scriptParser.hpp"
#include "scriptTypes.hpp"

// Per-directive argument grammars. Port target: browser/js/core/scriptArgs.js.
namespace stencil::core::script {

  // Evaluator state a statement can read and change: the default unit and the line style.
  struct EvalState {
    std::string unit = "px";
    LineStyle style;
  };

  // Fills op.strs/toks/nums for @crop. Handles the key form and 1/2/4 positional insets.
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

  // @use <unit> | @use line <groups> — mutates `state`. Returns false on a hard error.
  // `isStencilUse` is set when the statement is `@use stencil …`, which the caller expands.
  bool argsUse(const Stmt& st, EvalState& state, bool& isStencilUse,
               std::vector<Diagnostic>& diags);

  SourceKind classifySource(const std::string& spec);

  std::string unquoteWord(const std::string& s);

  // Joins a statement's argument words back into one string (paths, names, targets).
  std::string joinWords(const std::vector<Token>& args);

}  // namespace stencil::core::script

#pragma once
#include "types.hpp"

#include <set>
#include <string_view>

// Diagnostic construction and the "did you mean" suggester.
// Port target: browser/js/core/script/diagnostics.js.
namespace stencil::core::script {

  struct Stmt;

  // Capped at MAX_EDITS+1, so a far-away candidate costs no more than a near one.
  int editDistance(std::string_view a, std::string_view b);

  // The closest candidate within two edits, or "" when nothing is close enough.
  std::string didYouMean(std::string_view word, const std::vector<std::string_view>& candidates);

  Diagnostic makeDiag(Severity sev, const std::string& code, const Token& at,
                      const std::string& message);

  Token tokenOfStmt(const Stmt& st);

  Diagnostic makeDiag(Severity sev, const std::string& code, const Stmt& at,
                      const std::string& message);

  // The span an argument error underlines: the first argument, or the directive itself.
  Token argErrorToken(const Stmt& st);

  // "file:line:col: error: message [CODE]" — the --script-check line the editors parse.
  std::string formatDiagnostic(const std::string& file, const Diagnostic& d);

  bool hasErrors(const std::vector<Diagnostic>& diagnostics);

  // The `@use stencil` statement an expansion came from: its directive's line, byte column, length.
  struct CallSite {
    int line;
    int col;
    int len;
  };

  // Diagnostics from `from` on came out of a template `call` expanded: each names that call and
  // carries it as its related span, and one already in `seen` (same code, same span) is dropped.
  void noteCallSite(std::vector<Diagnostic>& diags, std::size_t from, CallSite call,
                    std::set<std::string>& seen);

}  // namespace stencil::core::script

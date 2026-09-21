#pragma once
#include "scriptTypes.hpp"

#include <string_view>

// Diagnostic construction and the "did you mean" suggester.
// Port target: browser/js/core/script/scriptDiagnostics.js.
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

}  // namespace stencil::core::script

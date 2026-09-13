#pragma once
#include "scriptTypes.hpp"

// Diagnostic construction and the "did you mean" suggester.
// Port target: browser/js/core/scriptDiagnostics.js.
namespace stencil::core::script {

  // Capped at MAX_EDITS+1, so a far-away candidate costs no more than a near one.
  int editDistance(const std::string& a, const std::string& b);

  // The closest candidate within two edits, or "" when nothing is close enough.
  std::string didYouMean(const std::string& word, const std::vector<std::string>& candidates);

  Diagnostic makeDiag(Severity sev, const std::string& code, const Token& at,
                      const std::string& message);

  // "file:line:col: error: message [CODE]" — the --script-check line the editors parse.
  std::string formatDiagnostic(const std::string& file, const Diagnostic& d);

  bool hasErrors(const std::vector<Diagnostic>& diagnostics);

}  // namespace stencil::core::script

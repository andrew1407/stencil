#pragma once
#include "scriptTypes.hpp"

// .stc tokenizer. Port target: browser/js/core/scriptLexer.js.
namespace stencil::core::script {

  struct LexResult {
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;
  };

  // Newline and ';' both end a statement and are emitted as PUNCT so the parser can
  // split on either. `len` is the byte length of `text` as written.
  LexResult lexScript(const char* text, int len);

  // True when `word` is a hex colour (#rgb / #rgba / #rrggbb / #rrggbbaa), which is why
  // a '#' does not always open a comment.
  bool isHexColorWord(const std::string& word);

}  // namespace stencil::core::script

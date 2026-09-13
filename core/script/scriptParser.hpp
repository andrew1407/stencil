#pragma once
#include "scriptTypes.hpp"

// Tokens -> statements, grouped into raw blocks. Port target: browser/js/core/scriptParser.js.
namespace stencil::core::script {

  struct Stmt {
    std::string directive;      // lower-cased, without '@' — "crop", "use", "source"…
    std::vector<Token> args;    // argument tokens, comments and separators removed
    bool opensBlock = false;    // a trailing ':'
    int line = 1;
    int col = 1;
    int len = 0;
  };

  // A `@stencil name …:` definition. `arity` is the highest @n its body references.
  struct TemplateDef {
    std::string name;
    std::vector<Stmt> body;
    int arity = 0;
    int line = 1;
    int col = 1;
    int len = 0;
    bool used = false;
  };

  struct RawBlock {
    Stmt header;                // directive "" for the implicit project block
    std::vector<Stmt> body;
    bool implicit = true;
  };

  struct ParseResult {
    std::vector<RawBlock> blocks;
    std::vector<TemplateDef> templates;
    std::vector<Diagnostic> diagnostics;
  };

  // Every directive the language knows, for validation and did-you-mean.
  const std::vector<std::string>& directiveNames();

  ParseResult parseScript(const std::vector<Token>& tokens);

}  // namespace stencil::core::script

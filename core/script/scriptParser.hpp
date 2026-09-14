#pragma once
#include "scriptTypes.hpp"

#include <array>
#include <string_view>

// Tokens -> statements, grouped into raw blocks. Port target: browser/js/core/scriptParser.js.
namespace stencil::core::script {

  /* Resolved once by the parser, so nothing downstream re-compares words. Internal to the
   * C++: it never crosses the ABI. CROP..LAYOUT is the contiguous edit set. */
  enum class Directive {
    NONE = 0,
    SOURCE,
    STENCIL,
    USE,
    SAVE,
    FRAME,
    UNDO,
    REDO,
    CROP,
    FILTER,
    LINE,
    RECT,
    LAYOUT,
  };

  struct DirectiveWord {
    std::string_view word;
    Directive kind;
  };

  // In the order the JS twin's DIRECTIVES lists them — stc.tmLanguage.json asserts against it.
  inline constexpr std::array<DirectiveWord, 12> DIRECTIVE_WORDS = {{
      {"source", Directive::SOURCE},
      {"stencil", Directive::STENCIL},
      {"use", Directive::USE},
      {"crop", Directive::CROP},
      {"filter", Directive::FILTER},
      {"line", Directive::LINE},
      {"rect", Directive::RECT},
      {"layout", Directive::LAYOUT},
      {"save", Directive::SAVE},
      {"frame", Directive::FRAME},
      {"undo", Directive::UNDO},
      {"redo", Directive::REDO},
  }};

  inline bool isEditDirective(Directive d) {
    return d >= Directive::CROP && d <= Directive::LAYOUT;
  }

  struct Stmt {
    std::string directive;      // lower-cased, without '@' — "crop", "use", "source"…
    Directive kind = Directive::NONE;
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

  ParseResult parseScript(const std::vector<Token>& tokens);

}  // namespace stencil::core::script

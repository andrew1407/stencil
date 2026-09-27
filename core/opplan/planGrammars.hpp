#pragma once
#include <cstddef>
#include <string_view>

// The registry's token grammars (opRegistry.json `regexes`) matched by hand — core has no regex
// engine and std::regex throws. Each row records the source it matches with JS RegExp
// semantics; tests pin both to the registry and to generated/grammarProbes.json.
namespace stencil::core::opplan {

  enum class Grammar {
    CROP_TOKEN, CROP_ASPECT, PAGE_FORMAT, HEX, CSS_NAME, FORMULA_X, FORMULA_Y, HTTP_URL, URL_SCHEME,
  };

  struct GrammarRow {
    std::string_view name;
    std::string_view source;
    Grammar grammar;
  };

  // Every grammar, in registry order; nullptr for a name core does not match.
  const GrammarRow* grammarNamed(std::string_view name);
  std::size_t grammarCount();
  const GrammarRow& grammarAt(std::size_t i);

  // `s` is WTF-8; a non-ASCII code point never matches an ASCII class.
  bool matches(Grammar g, std::string_view s);

}  // namespace stencil::core::opplan

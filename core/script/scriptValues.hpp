#pragma once
#include "scriptTypes.hpp"

// Shared argument readers: length tokens, colours and point lists.
// Port target: browser/js/core/scriptValues.js.
namespace stencil::core::script {

  // A cursor over one statement's argument tokens.
  struct ArgCursor {
    const std::vector<Token>* args = nullptr;
    std::size_t i = 0;

    bool atEnd() const { return !args || i >= args->size(); }
    const Token& peek() const { return (*args)[i]; }
    const Token& at(std::size_t k) const { return (*args)[k]; }
    std::size_t size() const { return args ? args->size() : 0; }
  };

  bool isPunct(const Token& t, const char* text);
  void skipPunct(ArgCursor& c, const char* text);

  // True for a word the colour parser accepts (#hex, a CSS name, "transparent").
  bool isColorToken(const Token& t);

  /* Reads NUMBER [UNIT] and normalizes it into an absolute length token.
   * A bare number takes `defaultUnit`, so "23" under `@use %` becomes "23%" — which is
   * what makes a leading '-' mean "from the far edge" consistently. Returns false when
   * the cursor is not on a number. */
  bool readLength(ArgCursor& c, const std::string& defaultUnit, std::string& out);

  // Like readLength but reports whether the token carried its own unit.
  bool readLengthRaw(ArgCursor& c, std::string& number, std::string& unit);

  std::string applyUnit(const std::string& number, const std::string& unit,
                        const std::string& fallback);

  /* Reads "(x, y) (x, y) …" into flat [x0,y0,x1,y1,…] length tokens. A unit may attach to
   * a component, or to the pair after its ')': "(56, 90)cm". Commas between points are
   * optional. Appends a diagnostic and returns false on a malformed list. */
  bool readPointList(ArgCursor& c, const std::string& defaultUnit, std::vector<std::string>& out,
                     std::vector<Diagnostic>& diags);

  bool isUnitWord(const std::string& w);

}  // namespace stencil::core::script

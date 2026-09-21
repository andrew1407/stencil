#pragma once
#include "scriptTypes.hpp"

#include <string_view>

// Shared argument readers: length tokens, colours, point lists and small integers.
// Port target: browser/js/core/script/scriptValues.js.
namespace stencil::core::script {

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

  // Reads NUMBER [UNIT] into an absolute length token; a bare number takes `defaultUnit`,
  // which is what makes a leading '-' mean "from the far edge" consistently.
  bool readLength(ArgCursor& c, const std::string& defaultUnit, std::string& out);

  bool readLengthRaw(ArgCursor& c, std::string& number, std::string& unit);

  std::string applyUnit(const std::string& number, const std::string& unit,
                        const std::string& fallback);

  /* Reads "(x, y) (x, y) …" into flat [x0,y0,x1,y1,…] length tokens. A unit may attach to a
   * component or to the pair after its ')': "(56, 90)cm". False on a malformed list. */
  bool readPointList(ArgCursor& c, const std::string& defaultUnit, std::vector<std::string>& out,
                     std::vector<Diagnostic>& diags);

  // The only length units the grammar knows; the lexer splits a number on this table too.
  bool isUnitWord(std::string_view w);

  std::string unquoteWord(const std::string& s);

  // Joins a statement's argument words back into one string (paths, names, targets).
  std::string joinWords(const std::vector<Token>& args);

  // Replaces std::atoi, which is undefined on an overflowing digit run.
  int parseIntClamped(const std::string& text);

}  // namespace stencil::core::script

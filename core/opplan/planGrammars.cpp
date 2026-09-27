#include "planGrammars.hpp"

#include "jsText.hpp"
#include "text.hpp"

#include <array>

namespace stencil::core::opplan {

  namespace {
    constexpr std::array<GrammarRow, 9> GRAMMARS{{
        {"CROP_TOKEN", "^-?(\\d+(\\.\\d+)?|\\.\\d+)(%|px|cm|in)?$", Grammar::CROP_TOKEN},
        {"CROP_ASPECT", "^0*[1-9]\\d*:0*[1-9]\\d*$", Grammar::CROP_ASPECT},
        {"PAGE_FORMAT", "^[abc](10|[0-9])$", Grammar::PAGE_FORMAT},
        {"HEX", "^#[0-9a-fA-F]{6}$", Grammar::HEX},
        {"CSS_NAME", "^[a-zA-Z]+$", Grammar::CSS_NAME},
        {"FORMULA_X", "^[0-9x+\\-*/(). ]+$", Grammar::FORMULA_X},
        {"FORMULA_Y", "^[0-9y+\\-*/(). ]+$", Grammar::FORMULA_Y},
        {"HTTP_URL", "^[hH][tT][tT][pP][sS]?://\\S+$", Grammar::HTTP_URL},
        {"URL_SCHEME", "^[a-zA-Z][a-zA-Z0-9+.\\-]*://", Grammar::URL_SCHEME},
    }};

    bool digit(char c) { return c >= '0' && c <= '9'; }
    bool alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
    bool hexDigit(char c) { return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

    std::size_t digitRun(std::string_view s, std::size_t i) {
      std::size_t n = 0;
      while (i + n < s.size() && digit(s[i + n])) ++n;
      return n;
    }

    // -?(\d+(\.\d+)?|\.\d+)(%|px|cm|in)?
    bool cropToken(std::string_view s) {
      std::size_t i = s.size() > 0 && s[0] == '-' ? 1 : 0;
      const std::size_t whole = digitRun(s, i);
      i += whole;
      if (i < s.size() && s[i] == '.') {
        const std::size_t frac = digitRun(s, i + 1);
        if (frac == 0) return false;
        i += 1 + frac;
      } else if (whole == 0) {
        return false;
      }
      const std::string_view unit = s.substr(i);
      return unit.empty() || unit == "%" || unit == "px" || unit == "cm" || unit == "in";
    }

    // 0*[1-9]\d* — digits, not all of them zero.
    bool positiveInt(std::string_view s) {
      return !s.empty() && digitRun(s, 0) == s.size() && s.find_first_not_of('0') != std::string_view::npos;
    }

    bool allOf(std::string_view s, bool (*ok)(char)) {
      for (char c : s)
        if (!ok(c)) return false;
      return !s.empty();
    }

    bool formula(std::string_view s, char axis) {
      if (s.empty()) return false;
      for (char c : s)
        if (!digit(c) && c != axis && std::string_view("+-*/(). ").find(c) == std::string_view::npos)
          return false;
      return true;
    }

    // [hH][tT][tT][pP][sS]?://\S+ — \S is any code unit outside JS WhiteSpace + LineTerminator.
    bool httpUrl(std::string_view s) {
      std::size_t i = 0;
      if (s.size() < 4 || toLowerAscii(s.substr(0, 4)) != "http") return false;
      i = 4;
      if (i < s.size() && (s[i] == 's' || s[i] == 'S')) ++i;
      if (s.substr(i, 3) != "://" || i + 3 == s.size()) return false;
      for (i += 3; i < s.size();) {
        const json::CodePoint c = json::codePointAt(s, i);
        if (json::isJsSpace(c.value)) return false;
        i += c.bytes;
      }
      return true;
    }

    // [a-zA-Z][a-zA-Z0-9+.\-]*:// as a prefix.
    bool urlScheme(std::string_view s) {
      if (s.empty() || !alpha(s[0])) return false;
      std::size_t i = 1;
      while (i < s.size() && (alpha(s[i]) || digit(s[i]) || s[i] == '+' || s[i] == '.' || s[i] == '-')) ++i;
      return s.substr(i, 3) == "://";
    }
  }  // namespace

  const GrammarRow* grammarNamed(std::string_view name) {
    for (const GrammarRow& row : GRAMMARS)
      if (row.name == name) return &row;
    return nullptr;
  }

  std::size_t grammarCount() { return GRAMMARS.size(); }
  const GrammarRow& grammarAt(std::size_t i) { return GRAMMARS[i < GRAMMARS.size() ? i : 0]; }

  bool matches(Grammar g, std::string_view s) {
    switch (g) {
      case Grammar::CROP_TOKEN: return cropToken(s);
      case Grammar::CROP_ASPECT: {
        const std::size_t colon = s.find(':');
        return colon != std::string_view::npos && positiveInt(s.substr(0, colon)) &&
               positiveInt(s.substr(colon + 1));
      }
      case Grammar::PAGE_FORMAT:
        return (s.size() == 2 || s.size() == 3) && (s[0] == 'a' || s[0] == 'b' || s[0] == 'c') &&
               (s.size() == 2 ? digit(s[1]) : s.substr(1) == "10");
      case Grammar::HEX: return s.size() == 7 && s[0] == '#' && allOf(s.substr(1), hexDigit);
      case Grammar::CSS_NAME: return allOf(s, alpha);
      case Grammar::FORMULA_X: return formula(s, 'x');
      case Grammar::FORMULA_Y: return formula(s, 'y');
      case Grammar::HTTP_URL: return httpUrl(s);
      case Grammar::URL_SCHEME: return urlScheme(s);
    }
    return false;
  }

}  // namespace stencil::core::opplan

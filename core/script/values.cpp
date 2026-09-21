#include "values.hpp"

#include "colorNames.hpp"
#include "diagnostics.hpp"
#include "lexer.hpp"
#include "text.hpp"

#include <climits>
#include <cstdlib>

namespace stencil::core::script {

  namespace {
    constexpr std::string_view UNIT_WORDS[] = {"px", "cm", "mm", "in", "%"};
  }  // namespace

  bool isUnitWord(std::string_view w) {
    const std::string u = toLowerAscii(w);
    for (std::string_view unit : UNIT_WORDS)
      if (u == unit) return true;
    return false;
  }

  std::string unquoteWord(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
    return s;
  }

  std::string joinWords(const std::vector<Token>& args) {
    std::string out;
    for (const Token& t : args) {
      if (t.kind == TokenKind::PUNCT) continue;
      if (!out.empty()) out.push_back(' ');
      out += unquoteWord(t.text);
    }
    return out;
  }

  int parseIntClamped(const std::string& text) {
    const long v = std::strtol(text.c_str(), nullptr, 10);
    if (v > INT_MAX) return INT_MAX;
    if (v < INT_MIN) return INT_MIN;
    return static_cast<int>(v);
  }

  bool isPunct(const Token& t, const char* text) {
    return t.kind == TokenKind::PUNCT && t.text == text;
  }

  void skipPunct(ArgCursor& c, const char* text) {
    while (!c.atEnd() && isPunct(c.peek(), text)) ++c.i;
  }

  bool isColorToken(const Token& t) {
    if (t.kind == TokenKind::COLOR) return isHexColorWord(t.text);
    if (t.kind != TokenKind::IDENT) return false;
    return parseColor(t.text).has_value();
  }

  std::string applyUnit(const std::string& number, const std::string& unit,
                        const std::string& fallback) {
    const std::string u = unit.empty() ? fallback : toLowerAscii(unit);
    if (u.empty() || u == "px") return number + "px";
    return number + u;
  }

  bool readLengthRaw(ArgCursor& c, std::string& number, std::string& unit) {
    if (c.atEnd() || c.peek().kind != TokenKind::NUMBER) return false;
    const Token& num = c.peek();
    number = num.text;
    unit.clear();
    ++c.i;
    if (!c.atEnd() && c.peek().kind == TokenKind::UNIT && c.peek().line == num.line &&
        c.peek().col == num.col + num.len) {
      unit = c.peek().text;
      ++c.i;
    }
    return true;
  }

  bool readLength(ArgCursor& c, const std::string& defaultUnit, std::string& out) {
    std::string number, unit;
    if (!readLengthRaw(c, number, unit)) return false;
    out = applyUnit(number, unit, defaultUnit);
    return true;
  }

  namespace {

    // A unit word sitting right after ')' applies to the whole pair.
    bool takePairUnit(ArgCursor& c, std::string& unit) {
      if (c.atEnd()) return false;
      const Token& t = c.peek();
      const bool unitLike = t.kind == TokenKind::UNIT ||
                            (t.kind == TokenKind::IDENT && isUnitWord(t.text));
      if (!unitLike) return false;
      unit = toLowerAscii(t.text);
      ++c.i;
      return true;
    }

  }  // namespace

  bool readPointList(ArgCursor& c, const std::string& defaultUnit, std::vector<std::string>& out,
                     std::vector<Diagnostic>& diags) {
    int points = 0;
    skipPunct(c, ",");
    while (!c.atEnd()) {
      if (!isPunct(c.peek(), "(")) {
        diags.push_back(makeDiag(Severity::ERROR, "E_BAD_TOKEN", c.peek(),
                                 "expected a point '(x, y)', found '" + c.peek().text + "'"));
        return false;
      }
      const Token open = c.peek();
      ++c.i;

      std::string xn, xu, yn, yu;
      if (!readLengthRaw(c, xn, xu)) {
        diags.push_back(makeDiag(Severity::ERROR, "E_BAD_TOKEN",
                                 c.atEnd() ? open : c.peek(), "expected a number for x"));
        return false;
      }
      skipPunct(c, ",");
      if (!readLengthRaw(c, yn, yu)) {
        diags.push_back(makeDiag(Severity::ERROR, "E_BAD_TOKEN",
                                 c.atEnd() ? open : c.peek(), "expected a number for y"));
        return false;
      }
      if (c.atEnd() || !isPunct(c.peek(), ")")) {
        diags.push_back(makeDiag(Severity::ERROR, "E_BAD_TOKEN",
                                 c.atEnd() ? open : c.peek(), "expected ')' to close the point"));
        return false;
      }
      ++c.i;

      std::string pairUnit;
      takePairUnit(c, pairUnit);
      const std::string fallbackX = xu.empty() && !pairUnit.empty() ? pairUnit : defaultUnit;
      const std::string fallbackY = yu.empty() && !pairUnit.empty() ? pairUnit : defaultUnit;
      out.push_back(applyUnit(xn, xu, fallbackX));
      out.push_back(applyUnit(yn, yu, fallbackY));

      if (++points > MAX_POINTS_PER_LINE) {
        diags.push_back(makeDiag(Severity::ERROR, "E_LIMIT_POINTS", open,
                                 "too many points in one shape"));
        return false;
      }
      skipPunct(c, ",");
    }
    return true;
  }

}  // namespace stencil::core::script

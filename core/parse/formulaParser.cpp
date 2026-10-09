#include "formulaParser.hpp"
#include "decimal.hpp"
#include "formulaContext.hpp"
#include <cmath>
#include <limits>

namespace stencil::core {

  namespace {

    // ASCII only, as formulaEngine.js spells them: <cctype> follows a locale the host may set.
    bool isDigit(char c) { return c >= '0' && c <= '9'; }
    bool isNameStart(char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    bool isNamePart(char c) { return isNameStart(c) || isDigit(c); }
    bool isSpace(char c) {
      return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    }

    // Math.pow, which differs from std::pow on a NaN exponent and on (±1) ** ±Infinity.
    double jsPow(double base, double exp) {
      if (std::isnan(exp) || (std::fabs(base) == 1.0 && std::isinf(exp)))
        return std::numeric_limits<double>::quiet_NaN();
      return std::pow(base, exp);
    }

    // On a syntax error `ok` clears and the parse unwinds with a zero result.
    class Eval {
     public:
      Eval(const std::string& src, char varName, double varValue, const FormulaContext& ctx)
        : src(src), var(varName), val(varValue), ctx(ctx) {}

      bool run(double& out) {
        const double v = parseExpr();
        skipSpaces();
        if (!ok || pos != src.size()) return false;
        out = v;
        return true;
      }

     private:
      const std::string& src;
      char var;
      double val;
      const FormulaContext& ctx;
      std::size_t pos = 0;
      bool ok = true;
      int depth = 0;

      // Recursion cap: untrusted '(' runs must not overflow the stack; past it the parse is
      // invalid (-> identity). Identical to formulaEngine.js MAX_DEPTH so wasm and JS agree.
      static constexpr int MAX_DEPTH = 256;

      // Decrements on unwind, so sibling subexpressions don't accumulate depth.
      struct DepthGuard {
        int& d;
        bool ok;
        explicit DepthGuard(int& depth) : d(depth), ok(++depth <= MAX_DEPTH) {}
        ~DepthGuard() { --d; }
      };

      void skipSpaces() {
        while (pos < src.size() && isSpace(src[pos])) ++pos;
      }

      char peek() {
        skipSpaces();
        return pos < src.size() ? src[pos] : '\0';
      }

      bool match(char a, char b) {  // two-char operator like **
        skipSpaces();
        if (pos + 1 < src.size() && src[pos] == a && src[pos + 1] == b) {
          pos += 2;
          return true;
        }
        return false;
      }

      bool match(char a) {
        skipSpaces();
        if (pos < src.size() && src[pos] == a) {
          ++pos;
          return true;
        }
        return false;
      }

      double parseExpr() {
        DepthGuard g(depth);
        if (!g.ok) { ok = false; return 0.0; }
        double v = parseTerm();
        while (ok) {
          if (match('+')) v += parseTerm();
          else if (match('-')) v -= parseTerm();
          else break;
        }
        return v;
      }

      double parseTerm() {
        // parsePower has already consumed any '**', so '*' here is never half of one.
        double v = parseUnary();
        while (ok) {
          if (match('*')) v *= parseUnary();
          else if (match('/')) v /= parseUnary();
          else break;
        }
        return v;
      }

      double parseUnary() {
        DepthGuard g(depth);
        if (!g.ok) { ok = false; return 0.0; }
        skipSpaces();
        if (match('+')) return parseUnary();
        if (match('-')) return -parseUnary();
        return parsePower();
      }

      double parsePower() {
        double base = parsePrimary();
        if (match('*', '*')) {           // right-associative: 2 ** 3 ** 2
          const double exp = parseUnary();
          return jsPow(base, exp);
        }
        return base;
      }

      double parsePrimary() {
        if (match('(')) {
          const double v = parseExpr();
          if (!match(')')) ok = false;
          return v;
        }
        const char c = peek();
        if (isDigit(c) || c == '.') {
          return parseNumber();
        }
        if (isNameStart(c)) {
          return parseIdentifier();
        }
        ok = false;
        return 0.0;
      }

      double parseNumber() {
        skipSpaces();
        const std::size_t start = pos;
        while (pos < src.size() && (isDigit(src[pos]) || src[pos] == '.')) ++pos;
        // optional exponent: e / E [+/-] digits
        if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
          std::size_t save = pos;
          ++pos;
          if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) {
            ++pos;
          }
          if (pos < src.size() && isDigit(src[pos])) {
            while (pos < src.size() && isDigit(src[pos])) ++pos;
          } else {
            pos = save;  // not an exponent after all
          }
        }
        // parseFloat's reading: the longest numeric prefix ("1.2.3" is 1.2), and no infinity.
        const auto v = parseDecimalPrefix(std::string_view(src).substr(start, pos - start));
        if (!v || !std::isfinite(*v)) {
          ok = false;
          return 0.0;
        }
        return *v;
      }

      // Longest run wins: `PAGE_WIDTHS` is one unknown name, not a constant plus junk.
      double parseIdentifier() {
        skipSpaces();
        const std::size_t start = pos;
        while (pos < src.size() && isNamePart(src[pos])) ++pos;
        double v = 0.0;
        if (formulaConstant(ctx, src.substr(start, pos - start), var, val, v)) return v;
        ok = false;
        return 0.0;
      }
    };

  }  // namespace

  std::optional<double> FormulaParser::evaluate(const std::string& expr, char varName,
                                                double varValue,
                                                const FormulaContext& ctx) {
    if (expr.size() > MAX_CHARS) return std::nullopt;
    Eval e(expr, varName, varValue, ctx);
    double out = 0.0;
    if (!e.run(out)) return std::nullopt;
    if (!std::isfinite(out)) return std::nullopt;
    return out;
  }

  std::optional<double> FormulaParser::evaluate(const std::string& expr,
                                                char varName,
                                                double varValue) {
    return evaluate(expr, varName, varValue, FormulaContext{});
  }

  bool FormulaParser::validate(const std::string& expr, char varName) {
    if (isBlankFormula(expr)) return true;  // empty = identity = valid
    return evaluate(expr, varName, 1.0).has_value();
  }

  double FormulaParser::apply(const std::string& expr, char varName,
                              double value, bool allowFormulas) {
    if (!allowFormulas || isBlankFormula(expr)) return value;
    const auto result = evaluate(expr, varName, value);
    return result.has_value() ? *result : value;
  }

}

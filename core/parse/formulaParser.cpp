#include "formulaParser.hpp"
#include <cctype>
#include <cmath>

namespace stencil::core {

  namespace {

    // On a syntax error `ok` clears and the parse unwinds with a zero result.
    class Eval {
     public:
      Eval(const std::string& src, char varName, double varValue)
        : src(src), var(varName), val(varValue) {}

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
        while (pos < src.size() &&
               std::isspace(static_cast<unsigned char>(src[pos]))) {
          ++pos;
        }
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
          return std::pow(base, exp);
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
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
          return parseNumber();
        }
        if (std::isalpha(static_cast<unsigned char>(c))) {
          return parseIdentifier();
        }
        ok = false;
        return 0.0;
      }

      double parseNumber() {
        skipSpaces();
        const std::size_t start = pos;
        while (pos < src.size() &&
               (std::isdigit(static_cast<unsigned char>(src[pos])) ||
                src[pos] == '.')) {
          ++pos;
        }
        // optional exponent: e / E [+/-] digits
        if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
          std::size_t save = pos;
          ++pos;
          if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) {
            ++pos;
          }
          if (pos < src.size() &&
              std::isdigit(static_cast<unsigned char>(src[pos]))) {
            while (pos < src.size() &&
                   std::isdigit(static_cast<unsigned char>(src[pos]))) {
              ++pos;
            }
          } else {
            pos = save;  // not an exponent after all
          }
        }
        try {
          return std::stod(src.substr(start, pos - start));
        } catch (...) {
          ok = false;
          return 0.0;
        }
      }

      double parseIdentifier() {
        skipSpaces();
        const std::size_t start = pos;
        while (pos < src.size() &&
               std::isalpha(static_cast<unsigned char>(src[pos]))) {
          ++pos;
        }
        const std::string ident = src.substr(start, pos - start);
        // Only the bound variable; any other name (`foo`) is a parse error.
        if (ident.size() == 1 && ident[0] == var) return val;
        ok = false;
        return 0.0;
      }
    };

    bool isBlank(const std::string& s) {
      for (char c : s) {
        if (!std::isspace(static_cast<unsigned char>(c))) return false;
      }
      return true;
    }

  }  // namespace

  std::optional<double> FormulaParser::evaluate(const std::string& expr,
                                                char varName,
                                                double varValue) {
    Eval e(expr, varName, varValue);
    double out = 0.0;
    if (!e.run(out)) return std::nullopt;
    if (!std::isfinite(out)) return std::nullopt;
    return out;
  }

  bool FormulaParser::validate(const std::string& expr, char varName) {
    if (isBlank(expr)) return true;  // empty = identity = valid
    return evaluate(expr, varName, 1.0).has_value();
  }

  double FormulaParser::apply(const std::string& expr, char varName,
                              double value, bool allowFormulas) {
    if (!allowFormulas || isBlank(expr)) return value;
    const auto result = evaluate(expr, varName, value);
    return result.has_value() ? *result : value;
  }

}

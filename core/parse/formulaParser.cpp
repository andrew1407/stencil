#include "formulaParser.hpp"
#include <cctype>
#include <cmath>

namespace stencil::core {

  namespace {

    // Recursive-descent evaluator over a single expression string. On any
    // syntax error it sets `ok = false` and unwinds with a zero result; the
    // caller treats !ok (or a non-finite value) as "invalid".
    class Eval {
     public:
      Eval(const std::string& src, char varName, double varValue)
        : src_(src), var_(varName), val_(varValue) {}

      // Parse a full expression and require that all input was consumed.
      bool run(double& out) {
        const double v = parseExpr();
        skipSpaces();
        if (!ok_ || pos_ != src_.size()) return false;
        out = v;
        return true;
      }

     private:
      const std::string& src_;
      char var_;
      double val_;
      std::size_t pos_ = 0;
      bool ok_ = true;
      int depth_ = 0;

      // Cap recursion depth so an adversarial deeply-nested input (e.g. thousands
      // of '(' or unary signs, reachable from untrusted layout JSON / console /
      // CLI --formula) can't overflow the stack. Past the cap the parse is invalid
      // (→ identity), matching the "invalid input never misbehaves" contract. Kept
      // identical to formulaEngine.js's MAX_DEPTH so wasm and the JS fallback agree.
      static constexpr int kMaxDepth = 256;

      // RAII depth counter: increments on entry, decrements on unwind so sibling
      // subexpressions don't accumulate depth.
      struct DepthGuard {
        int& d;
        bool ok;
        explicit DepthGuard(int& depth) : d(depth), ok(++depth <= kMaxDepth) {}
        ~DepthGuard() { --d; }
      };

      void skipSpaces() {
        while (pos_ < src_.size() &&
               std::isspace(static_cast<unsigned char>(src_[pos_]))) {
          ++pos_;
        }
      }

      char peek() {
        skipSpaces();
        return pos_ < src_.size() ? src_[pos_] : '\0';
      }

      bool match(char a, char b) {  // two-char operator like **
        skipSpaces();
        if (pos_ + 1 < src_.size() && src_[pos_] == a && src_[pos_ + 1] == b) {
          pos_ += 2;
          return true;
        }
        return false;
      }

      bool match(char a) {
        skipSpaces();
        if (pos_ < src_.size() && src_[pos_] == a) {
          ++pos_;
          return true;
        }
        return false;
      }

      double parseExpr() {
        DepthGuard g(depth_);
        if (!g.ok) { ok_ = false; return 0.0; }
        double v = parseTerm();
        while (ok_) {
          if (match('+')) v += parseTerm();
          else if (match('-')) v -= parseTerm();
          else break;
        }
        return v;
      }

      double parseTerm() {
        // '**' is consumed inside parsePower (reached via parseUnary), so the
        // cursor never sits on '**' when this loop tests for '*'.
        double v = parseUnary();
        while (ok_) {
          if (match('*')) v *= parseUnary();
          else if (match('/')) v /= parseUnary();
          else break;
        }
        return v;
      }

      double parseUnary() {
        DepthGuard g(depth_);
        if (!g.ok) { ok_ = false; return 0.0; }
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
          if (!match(')')) ok_ = false;
          return v;
        }
        const char c = peek();
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
          return parseNumber();
        }
        if (std::isalpha(static_cast<unsigned char>(c))) {
          return parseIdentifier();
        }
        ok_ = false;
        return 0.0;
      }

      double parseNumber() {
        skipSpaces();
        const std::size_t start = pos_;
        while (pos_ < src_.size() &&
               (std::isdigit(static_cast<unsigned char>(src_[pos_])) ||
                src_[pos_] == '.')) {
          ++pos_;
        }
        // optional exponent: e / E [+/-] digits
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
          std::size_t save = pos_;
          ++pos_;
          if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) {
            ++pos_;
          }
          if (pos_ < src_.size() &&
              std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
            while (pos_ < src_.size() &&
                   std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
              ++pos_;
            }
          } else {
            pos_ = save;  // not an exponent after all
          }
        }
        try {
          return std::stod(src_.substr(start, pos_ - start));
        } catch (...) {
          ok_ = false;
          return 0.0;
        }
      }

      double parseIdentifier() {
        skipSpaces();
        const std::size_t start = pos_;
        while (pos_ < src_.size() &&
               std::isalpha(static_cast<unsigned char>(src_[pos_]))) {
          ++pos_;
        }
        const std::string ident = src_.substr(start, pos_ - start);
        // Only the single bound variable is allowed; any other name (a function
        // such as `foo`, or a stray identifier) is a parse error.
        if (ident.size() == 1 && ident[0] == var_) return val_;
        ok_ = false;
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

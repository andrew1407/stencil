#include "numericInput.hpp"

#include <QRegularExpression>
#include <cmath>

namespace stencil::gui {

  namespace {

    // Same structure as browser/js/ui/numericInput.js and the core formula parser:
    // expr → term → unary → power → primary, unary ABOVE power so "-2 ** 2" is -(2**2).
    struct Parser {
      const QString& s;
      int i = 0;
      bool ok = true;

      explicit Parser(const QString& src) : s(src) {}

      void skip() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
      }
      bool match(QChar a) {
        skip();
        if (i < s.size() && s[i] == a) { ++i; return true; }
        return false;
      }
      bool matchPow() {  // '**' or the friendlier '^'
        skip();
        if (i + 1 < s.size() && s[i] == '*' && s[i + 1] == '*') { i += 2; return true; }
        if (i < s.size() && s[i] == '^') { ++i; return true; }
        return false;
      }
      bool nextIsMulDiv(QChar& op) {
        skip();
        if (i >= s.size()) return false;
        if (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '*') return false;
        if (s[i] == '*' || s[i] == '/') { op = s[i]; return true; }
        return false;
      }

      double expr() {
        double v = term();
        for (;;) {
          skip();
          if (match('+')) v += term();
          else if (match('-')) v -= term();
          else return v;
          if (!ok) return 0.0;
        }
      }
      double term() {
        double v = unary();
        for (;;) {
          QChar op;
          if (!nextIsMulDiv(op)) return v;
          ++i;  // consume the operator we peeked
          const double rhs = unary();
          if (!ok) return 0.0;
          if (op == '/') {
            if (rhs == 0.0) { ok = false; return 0.0; }  // div-by-zero → invalid
            v /= rhs;
          } else {
            v *= rhs;
          }
        }
      }
      double unary() {
        skip();
        if (match('+')) return unary();
        if (match('-')) return -unary();
        return power();
      }
      double power() {
        const double base = primary();
        if (!ok) return 0.0;
        if (matchPow()) return std::pow(base, unary());
        return base;
      }
      double primary() {
        skip();
        if (match('(')) {
          const double v = expr();
          if (!ok) return 0.0;
          if (!match(')')) { ok = false; return 0.0; }
          return v;
        }
        const int start = i;
        while (i < s.size() && s[i].isDigit()) ++i;
        if (i < s.size() && s[i] == '.') {
          ++i;
          while (i < s.size() && s[i].isDigit()) ++i;
        }
        if (i == start) { ok = false; return 0.0; }
        bool numOk = false;
        const double v = s.mid(start, i - start).toDouble(&numOk);
        if (!numOk) { ok = false; return 0.0; }
        return v;
      }
    };

    const QRegularExpression& continuesCurrent() {
      static const QRegularExpression re(QStringLiteral("^\\s*(\\*\\*|\\^|[*/])"));
      return re;
    }

  }  // namespace

  double evalNumericExpression(const QString& text, double current, bool* ok) {
    if (ok) *ok = false;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return 0.0;
    const QString src = continuesCurrent().match(trimmed).hasMatch()
                            ? QString::number(current, 'g', 17) + trimmed
                            : trimmed;
    Parser p(src);
    const double v = p.expr();
    p.skip();
    if (!p.ok || p.i != p.s.size() || !std::isfinite(v)) return 0.0;  // trailing junk / bad
    if (ok) *ok = true;
    return v;
  }

  namespace {
    // Never Invalid — that would swallow the character the user just typed.
    QValidator::State exprState(const QString& input, double current) {
      if (input.trimmed().isEmpty()) return QValidator::Intermediate;
      bool ok = false;
      evalNumericExpression(input, current, &ok);
      return ok ? QValidator::Acceptable : QValidator::Intermediate;
    }
  }  // namespace

  QValidator::State ExprSpinBox::validate(QString& input, int&) const {
    return exprState(input, static_cast<double>(value()));
  }

  int ExprSpinBox::valueFromText(const QString& text) const {
    bool ok = false;
    const double v = evalNumericExpression(text, static_cast<double>(value()), &ok);
    if (!ok) return value();  // unparseable → keep what we had
    return static_cast<int>(std::llround(v));
  }

  QValidator::State ExprDoubleSpinBox::validate(QString& input, int&) const {
    return exprState(input, value());
  }

  double ExprDoubleSpinBox::valueFromText(const QString& text) const {
    bool ok = false;
    const double v = evalNumericExpression(text, value(), &ok);
    return ok ? v : value();
  }

}  // namespace stencil::gui

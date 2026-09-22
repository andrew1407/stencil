#pragma once
#include "formulaContext.hpp"
#include <optional>
#include <string>

// Recursive-descent arithmetic parser, twin of browser/js/core/parse/formulaEngine.js (no eval).
//   expr  := term (('+' | '-') term)*        term  := unary (('*' | '/') unary)*
//   unary := ('+' | '-') unary | power       power := primary ('**' unary)?  // right-assoc
//   primary := number | name | '(' expr ')'  name  := [A-Za-z_][A-Za-z0-9_]*
// A name is the bound variable or a FormulaContext constant; any other is a parse error,
// as is a non-finite result.
namespace stencil::core {

  struct FormulaParser {
    // Empty / whitespace is valid (identity); otherwise finite at var = 1.
    static bool validate(const std::string& expr, char varName = 'x');

    // Identity-on-error: `value` when disabled, empty, or invalid / non-finite.
    static double apply(const std::string& expr, char varName, double value,
                        bool allowFormulas);

    static std::optional<double> evaluate(const std::string& expr, char varName,
                                          double varValue);

    // The same three with named constants in reach. `value` still binds `varName` and is
    // the identity fallback; `ctx` carries the other axis, which validates at 1 when unset.
    static bool validate(const std::string& expr, const FormulaContext& ctx);

    static double apply(const std::string& expr, char varName, double value,
                        bool allowFormulas, const FormulaContext& ctx);

    static std::optional<double> evaluate(const std::string& expr, const FormulaContext& ctx);

    static std::optional<double> evaluate(const std::string& expr, char varName,
                                          double varValue, const FormulaContext& ctx);
  };

}

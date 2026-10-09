#pragma once
#include "formulaContext.hpp"
#include <cstddef>
#include <optional>
#include <string>

// Recursive-descent arithmetic parser, twin of browser/js/core/parse/formulaEngine.js (no eval).
//   expr  := term (('+' | '-') term)*        term  := unary (('*' | '/') unary)*
//   unary := ('+' | '-') unary | power       power := primary ('**' unary)?  // right-assoc
//   primary := number | name | '(' expr ')'  name  := [A-Za-z_][A-Za-z0-9_]*
// A name other than the bound axis or a FormulaContext constant, or a non-finite result, errs.
namespace stencil::core {

  struct FormulaParser {
    // Longest expression parsed; past it the formula is invalid. Canon is LIMITS.formulaMaxChars
    // in common/config/constants.json, drift-tested in browser/tests/core/parse/formula.test.js.
    static constexpr std::size_t MAX_CHARS = 1000;

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

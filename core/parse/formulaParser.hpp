#pragma once
#include <optional>
#include <string>

// Recursive-descent arithmetic parser, the twin of browser/js/core/formulaEngine.js
// (no eval on either side). Grammar, single variable `x` or `y`:
//
//   expr    := term   (('+' | '-') term)*
//   term    := unary  (('*' | '/') unary)*
//   unary   := ('+' | '-') unary | power
//   power   := primary ('**' unary)?          // right-associative
//   primary := number | var | '(' expr ')'
//
// Any other identifier is a parse error; a non-finite result is invalid.
namespace stencil::core {

  struct FormulaParser {
    // Empty / whitespace is valid (identity); otherwise finite at var = 1.
    static bool validate(const std::string& expr, char varName = 'x');

    // Identity-on-error: `value` when disabled, empty, or invalid / non-finite.
    static double apply(const std::string& expr, char varName, double value,
                        bool allowFormulas);

    static std::optional<double> evaluate(const std::string& expr, char varName,
                                          double varValue);
  };

}

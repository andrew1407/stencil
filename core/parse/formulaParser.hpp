#pragma once
#include <optional>
#include <string>

// A small, safe arithmetic formula parser — the C++ replacement for the
// browser app's `new Function(...)` / eval approach in
// browser/js/core/formulaEngine.js.
//
// Grammar (recursive descent), single variable `x` or `y`:
//
//   expr    := term   (('+' | '-') term)*
//   term    := unary  (('*' | '/') unary)*
//   unary   := ('+' | '-') unary | power
//   power   := primary ('**' unary)?          // right-associative
//   primary := number | var | '(' expr ')'
//
// Supports: ( )  +  -  *  /  **  and a single variable. No functions, no other
// identifiers — anything else is a parse error (matching the JS engine, where
// `foo(x)` is invalid). Division by zero / overflow yield a non-finite result,
// which is reported as invalid.
namespace stencil::core {

  // The parser holds no state, so its operations are static — callers invoke
  // FormulaParser::validate(...) etc. directly, with no instance.
  struct FormulaParser {
    // Empty / whitespace expression = valid (identity), matching the JS engine.
    // Otherwise valid iff it parses and evaluates to a finite number at var = 1.
    static bool validate(const std::string& expr, char varName = 'x');

    // Apply the transform to `value`. Returns `value` unchanged when formulas
    // are disabled, the expression is empty, or evaluation fails / is non-finite
    // (identity-on-error, exactly like FormulaEngine.apply).
    static double apply(const std::string& expr, char varName, double value,
                        bool allowFormulas);

    // Evaluate `expr` with the variable bound to `varValue`. nullopt on a parse
    // error or a non-finite result.
    static std::optional<double> evaluate(const std::string& expr, char varName,
                                          double varValue);
  };

}

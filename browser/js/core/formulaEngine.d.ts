// Formula transforms over one variable: a recursive-descent evaluator (never `eval` /
// `new Function`) for `+ - * / ** ( )`, port of core/parse/formulaParser.cpp. Each method is
// core.bind-ed: the wasm parser when loaded, the JS reference otherwise.

export declare class FormulaEngine {
  /** True for an empty formula (identity) or one that evaluates with the variable = 1. */
  validate: (expr: string, varName: string) => boolean;
  /** The transformed value, or `val` unchanged when formulas are off, empty, or invalid. */
  apply: (expr: string, varName: string, val: number, allowFormulas: boolean) => number;
}

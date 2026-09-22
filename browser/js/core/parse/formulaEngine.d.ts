// Formula transforms over a variable and the FormulaContext constants: a recursive-descent
// evaluator (never `eval` / `new Function`) for `+ - * / ** ( )`, port of
// core/parse/formulaParser.cpp. Each method is core.bind-ed: the wasm parser when loaded,
// the JS reference otherwise.
import type { FormulaContext } from './formulaContext.js';

export declare class FormulaEngine {
  /** True for an empty formula (identity) or one that evaluates with the variable = 1. */
  validate: (expr: string, varName: string) => boolean;
  /** The transformed value, or `val` unchanged when formulas are off, empty, or invalid. */
  apply: (expr: string, varName: string, val: number, allowFormulas: boolean) => number;
  /** As `validate`, with `ctx`'s constants in reach; an axis `ctx` leaves unset probes at 1. */
  validateCtx: (expr: string, ctx: FormulaContext) => boolean;
  /** As `apply`: `val` binds `varName`, `ctx` carries the other axis and the constants. */
  applyCtx: (expr: string, varName: string, val: number, allowFormulas: boolean,
    ctx: FormulaContext) => number;
}

import { core } from './stencilCore.js';

// ── Formula transforms (pure logic) ─────────────────────────────
// When the wasm core is loaded, validate/apply delegate to its recursive-descent
// parser (the same code the Qt desktop app runs) instead of the JS `new Function`
// path. The JS below is the reference and the fallback until wasm is ready.
export class FormulaEngine {
  // Validate a formula string — try evaluating it with x=1 (or y=1).
  // Empty = valid (identity).
  validate = core.bind('formulaValidate', (expr, varName) => {
    if (!expr || !expr.trim()) return true; // empty = valid (identity)
    try {
      const fn = new Function(varName, '"use strict"; return (' + expr + ');');
      const result = fn(1);
      return typeof result === 'number' && isFinite(result);
    } catch {
      return false;
    }
  });

  // Apply formula transform to a coordinate value; returns original if
  // formulas are disabled, the expression is empty, or evaluation fails.
  apply = core.bind('formulaApply', (expr, varName, val, allowFormulas) => {
    if (!allowFormulas || !expr || !expr.trim()) return val;
    try {
      const fn = new Function(varName, '"use strict"; return (' + expr + ');');
      const result = fn(val);
      return (typeof result === 'number' && isFinite(result)) ? result : val;
    } catch {
      return val;
    }
  });
}

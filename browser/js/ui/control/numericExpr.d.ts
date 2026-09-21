// Shapes for ui/numericExpr.js — the pure expression evaluator behind every numeric field.
// The module is byte-pinned with browser-extension/src/lib/control/numericExpr.js
// (browser-extension/tests/portParity.test.js); this file is not.

/**
 * Evaluate what the user typed into a numeric field: a finite number, or null when the
 * text is not a valid expression. A leading *, / or ** applies to `current`.
 */
export declare const evalNumericExpression: (text: string, current?: number) => number | null;

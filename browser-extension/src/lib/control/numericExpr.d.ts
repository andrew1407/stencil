// PORT of browser/js/ui/control/numericExpr.d.ts (browser-extension/tests/portParity.test.js).

/**
 * Evaluate what the user typed into a numeric field: a finite number, or null when the
 * text is not a valid expression. A leading *, / or ** applies to `current`.
 */
export declare const evalNumericExpression: (text: string, current?: number) => number | null;

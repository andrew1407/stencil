// The named values a formula may read besides its own axis (pure), twin of
// core/parse/formulaContext.hpp. Page fields are cm, image fields pixels; `unit` only picks
// the spelling PAGE_WIDTH / PAGE_HEIGHT report in.

/** Every field is optional: an absent or non-finite one leaves its names unknown. */
export interface FormulaContext {
  x?: number;
  y?: number;
  pageWidthCm?: number;
  pageHeightCm?: number;
  imageWidth?: number;
  imageHeight?: number;
  /** 'in' converts; every other word reads as cm. */
  unit?: string;
}

/** A blank formula is the identity: valid, and apply() returns its input unchanged. */
export declare const isBlankFormula: (s: unknown) => boolean;
/** The context with each unbound axis at 1, for validating without live coordinates. */
export declare const withProbeAxes: (ctx: FormulaContext | null | undefined) => FormulaContext;
/** One name resolved (case-sensitive), or null when unknown or unsupplied. */
export declare const formulaConstant: (ctx: FormulaContext | null | undefined, name: string,
  varName: string, varValue: number) => number | null;

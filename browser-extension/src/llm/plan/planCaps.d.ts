// A port of browser/js/llm/plan/planCaps.js: the §1 JSON caps over a parsed op plan.

/** UTF-8 bytes of a JS string; a lone surrogate counts as its U+FFFD. */
export declare const utf8Length: (s: string) => number;

export interface JsonLimitHit { limit: 'MAX_BYTES' | 'MAX_DEPTH' | 'MAX_NODES'; max: number; detail: string; }
/** The first cap a parsed plan exceeds (bytes, depth, values), or null. */
export declare const jsonLimit: (
  value: unknown, text: string, caps: { depth: number; bytes: number; nodes: number } | null,
) => JsonLimitHit | null;

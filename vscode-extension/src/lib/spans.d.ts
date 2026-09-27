// Shape of spans.js — engine byte spans (1-based) onto editor UTF-16 positions (0-based).
export declare const sourceLines: (text: string) => string[];
export declare const unitColumn: (lineText: string | undefined, col: number) => number;
export declare const unitSpan: (lines: string[] | undefined, span: { line: number; col: number; len: number }) =>
  { line: number; start: number; end: number };

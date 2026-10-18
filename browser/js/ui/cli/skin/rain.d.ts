// The matrix skin's digit rain over the scrollback rows.
export declare const stepHeads: (heads: number[], h: number, random: () => number) => number[];
export declare const rain: (pre: HTMLElement, opts: { cols: number; rows: number; random?: () => number;
  schedule?: (fn: () => void, ms: number) => unknown; onDone?: () => void }) => () => void;
export declare const gridOf: (pre: HTMLElement) => { cols: number; rows: number };

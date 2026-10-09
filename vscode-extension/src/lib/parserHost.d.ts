// Shape of parserHost.js — the memoized lazy loader of the parser copies.
import type * as parser from '../parser/index.js';
export declare const loadParser: (
  importer?: () => Promise<typeof parser>,
) => Promise<typeof parser>;
/** Every 1-based UTF-8 byte column of `line` as a 0-based UTF-16 index (the copies' own helper). */
export declare const columnIndex: (line: string) => { bytes: number; unitOf: (col: number) => number };

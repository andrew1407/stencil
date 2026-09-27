// Shape of parserHost.js — the memoized lazy loader of the parser copies.
import type * as parser from '../parser/index.js';
export declare const loadParser: (
  importer?: () => Promise<typeof parser>,
) => Promise<typeof parser>;
/** A 1-based UTF-8 byte column on `line` as a 0-based UTF-16 index (the copies' own helper). */
export declare const unitIndexOfColumn: (line: string, col: number) => number;
export declare const utf8Length: (s: string, from?: number, to?: number) => number;

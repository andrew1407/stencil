// Shape of parserHost.js — the memoized lazy loader of the parser copies.
import type * as parser from '../parser/index.js';
export declare const loadParser: (
  importer?: () => Promise<typeof parser>,
) => Promise<typeof parser>;

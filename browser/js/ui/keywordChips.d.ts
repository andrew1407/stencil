import type { MetaField } from './projectMetaModal.js';

/** One keyword, trimmed and lowercased with inner whitespace collapsed; '' when empty. */
export declare function normalizeKeyword(raw: unknown): string;

/** A stored list, cleaned and de-duplicated in order. */
export declare function parseKeywords(list: unknown): string[];

/** `raw` as ONE keyword, prepended to `list`; one already held moves first, never doubles. */
export declare function addKeywords(list: string[], raw: unknown): string[];

/** The keywords field: a one-line input over the chips already added. */
export declare const keywordChipsField: (opts: { placeholder: string }) => MetaField<string[]>;

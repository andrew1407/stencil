// Shapes for llm/opSchemaBase.js — the closure-free base of opSchema.js: value predicates, the
// SchemaError the checks throw, the message-path builders and the native cross-field rules.
// The extension ships a byte-identical copy of the module.

/** Where a value sits, for a message: `"x1" in spec`, `"label" in ask.options[2]`. */
export interface SchemaPath { root: string; key: string; container: string | null; }

export declare const isObj: (v: unknown) => boolean;
export declare const isFiniteNum: (v: unknown) => boolean;
export declare const isInt: (v: unknown) => boolean;
export declare const quoteList: (xs: ReadonlyArray<string | number | boolean>) => string;

/** Thrown inside the checks; the entry points re-throw it with the surface's prefix. */
export declare class SchemaError extends Error {}
export declare const bad: (why: string) => never;

export declare const where: (p: SchemaPath) => string;
export declare const label: (p: SchemaPath) => string;
export declare const child: (p: SchemaPath | null, key: string) => SchemaPath;
export declare const item: (p: SchemaPath, i: number) => SchemaPath;

/** The native folds an entry may name in `rules`, applied before the field checks. */
export declare const RULES: Record<string, (a: Record<string, unknown>) => Record<string, unknown>>;

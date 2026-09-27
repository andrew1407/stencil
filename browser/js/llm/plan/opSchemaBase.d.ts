// Shapes for llm/plan/opSchemaBase.js — the closure-free base of schema.js: value predicates, the
// SchemaError the checks throw, the message-path builders and the native cross-field rules.
// The extension ships a byte-identical copy of the module.

/** Where a value sits, for a message: `"x1" in spec`, `"label" in ask.options[2]`. */
export interface SchemaPath { root: string; key: string; container: string | null; }

export declare const isObj: (v: unknown) => boolean;
export declare const isFiniteNum: (v: unknown) => boolean;
export declare const isInt: (v: unknown) => boolean;
export declare const quoteList: (xs: ReadonlyArray<string | number | boolean>) => string;
/** An OWN property's value, else undefined — model keys never reach Object.prototype. */
export declare const own: (o: unknown, k: string) => unknown;

/** Thrown inside the checks; the entry points re-throw it with the surface's prefix. */
export declare class SchemaError extends Error {}
export declare const bad: (why: string) => never;

export declare const where: (p: SchemaPath) => string;
export declare const label: (p: SchemaPath) => string;
export declare const child: (p: SchemaPath | null, key: string) => SchemaPath;
export declare const item: (p: SchemaPath, i: number) => SchemaPath;

/** The declared keys present (deep-picked, trims applied) plus the registry defaults. */
export declare const pickFields: (obj: Record<string, unknown>, fields: Record<string, unknown>) => Record<string, unknown>;

/** The native folds an entry may name in `rules`, applied before the field checks. */
export declare const RULES: Record<string, (a: Record<string, unknown>) => Record<string, unknown>>;

/** The registry `surfaceRules` checks: true when the normalized action passes. */
export declare const SURFACE_RULES: Readonly<Record<string,
  (out: Record<string, unknown>, rule: Record<string, unknown>, natives: { knownColor?: (name: string) => boolean }) => boolean>>;

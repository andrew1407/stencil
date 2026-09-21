// Shapes for llm/profile.js — the extension's profile of the shared op registry
// (llm-contract.md §1, §8, §13): the table-driven schema, its limits, and the
// primitives every per-op validator (validate.js) shares. Pure — no DOM, no chrome.
import type { Schema, SchemaEntry } from './schema.js';

export interface OpLimits {
  actions: number;
  variants: number;
  layoutLines: number;
  stringChars: number;
  attachIndices: number;
  pinIndices: number;
  filterSearch: number;
  filterFormats: number;
  filterSize: number;
}

export declare const SCHEMA: Schema;
export declare const LIMITS: OpLimits;
export declare const ASK_LIMITS: Record<string, number>;
export declare const DEFAULT_CUSTOM_LABEL: string;
/** The §2 core-op set: dropped with a warning at the top level of an extension plan. */
export declare const CORE_OPS: Set<string>;
/** §13: never model-drivable, on any surface. */
export declare const FORBIDDEN_OPS: Set<string>;

export declare function isObj(v: unknown): v is Record<string, unknown>;
export declare function isStr(v: unknown, max?: number): v is string;
/** Always throws — `Invalid {op} action: {why}`. */
export declare function fail(op: string, why: string): never;
export declare function check(a: Record<string, unknown>, entry: SchemaEntry): Record<string, unknown>;
/** Throws unless `v` points into a listing of `listingLength` entries. */
export declare function validIndex(v: number, listingLength: number, op: string): number;

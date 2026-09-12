// Shapes for llm/opSchema.js — the registry-driven half of every op validator. The module
// is a byte-pinned port of browser/js/llm/opSchema.js (tests/portParity.test.js); this
// file documents what createSchema hands back, which the surface's own rules build on.

/** One op's entry in the registry, already filtered to this surface's profile. */
export interface SchemaEntry {
  op: string;
  [key: string]: unknown;
}

export interface Schema {
  /** The ops this surface registers, in registry order. */
  entries: SchemaEntry[];
  /** Ops the surface must refuse outright (contract §2). */
  forbidden: string[];
  /** Every cap the contract names, plus this surface's own block. */
  limits: Record<string, any>;
  /** Throws on anything the entry does not allow; returns the accepted action. */
  validateAction(action: unknown, entry: SchemaEntry): Record<string, unknown>;
  /** Applies the entry's declared defaults and coercions to a validated action. */
  normalize(action: Record<string, unknown>, entry: SchemaEntry): Record<string, unknown>;
}

export declare function createSchema(registry: unknown, surface: string): Schema;

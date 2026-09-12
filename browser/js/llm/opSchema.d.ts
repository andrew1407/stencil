// Registry-driven op-plan validation (llm-contract.md §1–§2, §8, §11): the generic half of
// every op validator, table-driven from config/llm/opRegistry.json. Pure — no DOM, no
// fetch. The extension ships a byte-identical copy of the module.

/** One key's declared shape in the registry (config/llm/opRegistry.README.md). */
export interface KeySpec {
  type: 'string' | 'integer' | 'number' | 'boolean' | 'array' | 'object';
  required?: boolean;
  enum?: ReadonlyArray<string | number | boolean>;
  /** [lo, hi]; either end may be null (open). */
  range?: [number | null, number | null];
  /** A number, or a dotted name into the registry's limits ("MAX_ACTIONS", "ask.label"). */
  maxChars?: number | string;
  trim?: boolean;
  nonEmpty?: boolean;
  blankOk?: boolean;
  literals?: string[];
  regex?: string | string[];
  regexBy?: { key: string; map: Record<string, string> };
  regexNot?: string;
  minItems?: number | string;
  maxItems?: number | string;
  items?: KeySpec;
  fields?: Record<string, KeySpec>;
  default?: unknown;
  /** Present only when the named sibling holds one of these values. */
  onlyWith?: Record<string, unknown[]>;
  requiredWith?: Record<string, unknown[]>;
  minFields?: number;
  allowUnknown?: boolean;
}

/** The cross-field presence rules a key map's holder may carry. */
export interface KeyHolder {
  keys: Record<string, KeySpec>;
  forms?: string[][];
  together?: string[][];
  exclusive?: string[][];
  minFields?: number;
  allowUnknown?: boolean;
  /** Names into RULES — native folds run before the field checks. */
  rules?: string[];
}

/** One op as the registry records it, before per-surface resolution. */
export interface RegistryOp extends KeyHolder {
  name: string;
  id: string;
  profiles: string[];
  surfaces?: string[];
  bullet: string | null;
  bulletVariants?: Record<string, string>;
  surfaceKeys?: Record<string, Record<string, KeySpec>>;
  flags?: Record<string, boolean>;
  surfaceFlags?: Record<string, Record<string, boolean>>;
  also?: string;
  alsoOrder?: number;
  requires?: string[];
}

/** One op's entry, resolved for this surface: its keys, prompt bullet and flags. */
export interface SchemaEntry extends RegistryOp {
  flags: Record<string, boolean>;
}

/** The registry's `limits` block: named caps plus the §11 ask caps. */
export interface SchemaLimits {
  MAX_ACTIONS: number; MAX_VARIANTS: number; MAX_LAYOUT_LINES: number; MAX_FRAME_INDICES: number;
  MAX_STRING_CHARS: number; MAX_SAVE_NAME: number; MAX_PATH_CHARS: number; MAX_RENAME_NAME: number;
  MAX_UNDO_STEPS: number;
  ask: { minOptions: number; maxOptions: number; question: number; label: number; answer: number };
  [group: string]: number | Record<string, number | string>;
}

/** config/llm/opRegistry.json, as createSchema reads it. */
export interface OpRegistry {
  $meta: { schemaVersion: number; surfaceProfiles: Record<string, string> };
  limits: SchemaLimits;
  /** Named grammars as RegExp sources; `describe` maps a name to its prose. */
  regexes: Record<string, string> & { describe: Record<string, string> };
  profiles: Record<string, { ops: string[]; surfaces?: string[] }>;
  ops: RegistryOp[];
  forbidden: { core: string[]; perSurface: Record<string, string[]> };
  ask: { defaultCustomLabel: string; schema: KeyHolder };
  opsets: Record<string, {
    ops: string[];
    failOps?: string[];
    overrides?: Record<string, { keys: Record<string, KeySpec>; rules?: string[] }>;
  }>;
  envelope: Record<string, KeySpec>;
}

export interface Schema {
  surface: string;
  profile: string;
  limits: SchemaLimits;
  /** The ops this surface registers, in registry (= prompt) order. */
  entries: SchemaEntry[];
  ops: Map<string, SchemaEntry>;
  /** Ops the surface must refuse outright (contract §13). */
  forbidden: Set<string>;
  regexes: Record<string, RegExp>;
  /** A number as is, or a dotted limit name resolved; throws on an unknown name. */
  limit(v: number | string): number;
  /** Throws "Invalid <op> action: …"; returns the action as validated (post-fold). */
  validateAction(a: unknown, entry: SchemaEntry): Record<string, unknown>;
  /** { op, ...declared keys present (deep-picked), defaults }. */
  normalize(v: Record<string, unknown>, entry: SchemaEntry): Record<string, unknown>;
  /** The §11 card's structure; throws "Invalid plan: …". */
  validateAsk(ask: unknown): void;
  normalizeAsk(ask: Record<string, unknown>): Record<string, unknown>;
  /** An entry to validate with, 'fail' for a listed-but-disallowed op, null for unknown. */
  opsetEntry(name: string, op: string): SchemaEntry | KeyHolder & { name: string } | 'fail' | null;
  envelope: Record<string, KeySpec>;
  /** Check one envelope slot ("actions" / "variants") shallowly; throws "Invalid plan: …". */
  checkEnvelope(v: unknown, key: string): void;
}

export declare const createSchema: (registry: OpRegistry, surface: string) => Schema;

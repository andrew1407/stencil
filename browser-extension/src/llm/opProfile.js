// ── Op-plan: the extension's profile of the shared registry (§1, §8, §13) ──
// The table every opPlan validator reads, the limits it enforces, and the primitives
// the per-op normalizers share. Pure — no DOM, no chrome, no fetch.

import REGISTRY from '../config/opRegistry.json' with { type: 'json' };
import { createSchema } from './opSchema.js';

// The checked-in copy of config/llm/opRegistry.json filtered to the extension profile (§8),
// drift-guarded by dataParity.test.js; this adds the listing-bound checks and the normalizers.
export const SCHEMA = createSchema(REGISTRY, 'extension');

// Limits — the same numbers in every client (contract §1) + the §8 attach/pin caps and
// the panel-settings caps, all read from the registry.
export const LIMITS = Object.freeze({
  actions: SCHEMA.limits.MAX_ACTIONS, variants: SCHEMA.limits.MAX_VARIANTS,
  layoutLines: SCHEMA.limits.MAX_LAYOUT_LINES, stringChars: SCHEMA.limits.MAX_STRING_CHARS,
  attachIndices: SCHEMA.limits.extension.attachIndices, pinIndices: SCHEMA.limits.extension.pinIndices,
  filterSearch: SCHEMA.limits.extension.filterSearch, filterFormats: SCHEMA.limits.extension.filterFormats,
  filterSize: SCHEMA.limits.extension.filterSize,
});

// §11 interactive replies — the same numbers as every other client.
export const ASK_LIMITS = Object.freeze({ ...SCHEMA.limits.ask });
export const DEFAULT_CUSTOM_LABEL = REGISTRY.ask.defaultCustomLabel;

// The full §2 core-op set: at the TOP level of an extension plan these are dropped
// with a warning (contract §8 — editing happens in the editor after `open`).
export const CORE_OPS = new Set(REGISTRY.opsets.extensionOpen.failOps);

export const isObj = (v) => v != null && typeof v === 'object' && !Array.isArray(v);
export const isStr = (v, max = LIMITS.stringChars) => typeof v === 'string' && v.length <= max;
export const fail = (op, why) => { throw new Error(`Invalid ${op} action: ${why}`); };

// Table-driven check + normalize for one action against its registry entry.
export const check = (a, entry) => SCHEMA.normalize(SCHEMA.validateAction(a, entry), entry);

// An image index (already an integer >= 0) must point into the current context listing.
export const validIndex = (v, listingLength, op) => {
  if (v >= listingLength) fail(op, `image index ${v} is out of range (the listing has ${listingLength} entries)`);
  return v;
};

// ── §13 forbidden ops: never model-drivable, on any surface. Teeth: a test pins that no
// registry entry uses these names, and chatController refuses them at execution.
export const FORBIDDEN_OPS = new Set(SCHEMA.forbidden);

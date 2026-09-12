// Shapes for llm/opValidate.js — the per-op validators and the op registry this
// surface exposes (contract §8 + §13): one ordered entry per op, table-driven checks
// from opProfile.js plus the listing-bound rules and normalizers only this surface has.

export interface OpRegistryEntry {
  name: string;
  /** The prompt line opPrompt.js assembles the "Available ops" section from. */
  bullet: string;
  validate: (
    action: Record<string, unknown>,
    listingLength: number,
    warnings: string[],
    tabsLength: number,
  ) => Record<string, unknown>;
  /** Marks a context-gathering op (§8 auto-continuation). */
  gather?: true;
  /** Marks one of the panel's own controls, not an image edit. */
  panelSettings?: true;
}

/** Assembled from the shared registry's extension entries, in prompt order. */
export declare const OP_REGISTRY: OpRegistryEntry[];
/** Validator lookup for parseOpPlan, keyed by op name. */
export declare const EXT_VALIDATORS: Record<string, OpRegistryEntry['validate']>;
/** The §8 auto-continuation set, derived from the registry's `gather` flag. */
export declare const GATHER_OPS: Set<string>;

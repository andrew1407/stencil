// Which wasm exports the app calls at runtime and which only tests/wasm drives, so a stale
// artifact missing a parity-only wrapper loses that op alone instead of the whole core.

/** Every export an op the app calls at runtime needs; one missing rejects the whole core. */
export declare const RUNTIME_EXPORTS: readonly string[];
/** Parity-only ops → their exports; `state` groups the handle classes and project rules. */
export declare const PARITY_EXPORTS: Readonly<Record<'distToSegment' | 'pageFormats' | 'pixelToPageRaw' | 'state', readonly string[]>>;
/** The symbols in `syms` the module does not expose as `_<sym>` functions. */
export declare const missingExports: (mod: Record<string, unknown>, syms: readonly string[]) => string[];
/** The parity-only ops whose exports are not all present. */
export declare const droppedParityOps: (mod: Record<string, unknown>) => Array<keyof typeof PARITY_EXPORTS>;

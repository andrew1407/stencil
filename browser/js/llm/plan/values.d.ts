// Shared op-plan value guards + resolvers — the small checks the validator, the executors
// and the plan runner all share.

export declare const isObj: (v: unknown) => v is Record<string, unknown>;
/** A string no longer than `max` (default LIMITS.stringChars). */
export declare const isStr: (v: unknown, max?: number) => v is string;
/** Variant labels name files/projects: trimmed, filesystem-safe, ≤ 40 chars, never empty. */
export declare const sanitizeLabel: (label: unknown) => string;
/**
 * §10 connect/disconnect: resolve `server` against a list the USER owns — exact URL match,
 * else a UNIQUE host match; anything else throws (naming `what`). The matched entry is
 * returned, so a saved server's stored token rides along. A plan can never add a host.
 */
export declare const resolveServer: <T extends string | { url: string }>(server: unknown, entries: readonly T[], what: string) => T;

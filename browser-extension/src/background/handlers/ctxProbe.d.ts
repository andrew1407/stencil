// Shapes for background/handlers/ctxProbe.js — records what the ctxTarget probe
// resolved and relabels/reveals the context-menu groups that depend on it.

/** How long a cold worker's first pin relabel waits for the pins cache, in ms. */
export declare const PIN_CACHE_WAIT_MS: number;
/** Keyed by the `MSG.CTX` channel name; synchronous but for a cold worker's first pin relabel. */
export declare const ctxProbeHandlers: Record<string, (msg: Record<string, unknown>, sender: { tab?: { id?: number; url?: string }; frameId?: number }) => void>;

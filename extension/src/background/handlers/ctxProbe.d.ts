// Shapes for background/handlers/ctxProbe.js — records what the ctxTarget probe
// resolved and relabels/reveals the context-menu groups that depend on it.

/** Keyed by the `MSG.CTX` channel name; the handler is synchronous (the menu is opening). */
export declare const ctxProbeHandlers: Record<string, (msg: Record<string, unknown>, sender: { tab?: { id?: number; url?: string }; frameId?: number }) => void>;

// Shapes for background/handlers/pageApi.js — window.stencil (page API) relays, plus
// two small utility messages. Every handler is fire-and-forget: the page asks, the
// worker does it, the port closes.

/** Keyed by the page-API channel names (MSG.PAGE_OPEN, .PAGE_PIN, .PAGE_CROP, …). */
export declare const pageApiHandlers: Record<string, (msg: Record<string, unknown>, sender?: { tab?: { id?: number; url?: string }; origin?: string; url?: string }) => void>;

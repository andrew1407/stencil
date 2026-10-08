// Shapes for background/handlers/storeWrites.js — the worker end of lib/prefs/writeChain.js.
// Request/response: `{ ok:true, value }` carries what the write returned; only the
// extension's own pages are answered.

/** Keyed by MSG.STORE_WRITE; `msg` is `{ store: 'pins' | 'ledger', op, args }`. */
export declare const storeWriteHandlers: Record<string, (msg: Record<string, unknown>, sender: { url?: string }, sendResponse: (res: unknown) => void) => true>;

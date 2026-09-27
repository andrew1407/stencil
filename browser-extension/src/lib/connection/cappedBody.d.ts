// Extension copy of browser/js/net/cappedBody.d.ts: a response body read under a byte cap,
// declared or streamed, as bytes or as JSON — the one bounded read page-derived bytes, a
// server's and a provider's reply go through. Byte-pinned (portParity).

/** 64 MiB: the most a guarded read or a server reply buffers. */
export declare const MAX_FETCH_BYTES: number;
/** 64 KiB: an error body, read only for its `message`. */
export declare const MAX_ERROR_BYTES: number;
export declare function cancelBody(resp: Response): Promise<void>;
/** The body, refused past `max` bytes whether declared or streamed. */
export declare function readCapped(resp: Response, max?: number): Promise<ArrayBuffer>;
/** The body as JSON under the same cap; a plain object with no stream reads through its own json(). */
export declare function readJsonCapped(resp: Response | { json(): Promise<unknown> }, max?: number): Promise<unknown>;

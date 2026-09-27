/** data:/blob: pass; http(s) passes unless the host is a localhost name or an address the table's
 * `fetch` policy blocks. */
export declare function isAllowedImageUrl(
  url: string, opts?: { allowLoopback?: boolean; allowSameHostAs?: string },
): boolean;
/** 64 MiB: the most any guarded read buffers. */
export declare const MAX_FETCH_BYTES: number;
export declare const BLOCKED_ADDRESS: string;
export declare const REDIRECT_REFUSED: string;
/** Refuses a blocked host up front and any redirect after, never following one; a redirected
 * http URL is asked for once more as its https twin. */
export declare function guardedFetch(
  url: string, guard?: { allowLoopback?: boolean; allowSameHostAs?: string }, init?: RequestInit,
): Promise<Response>;
/** The body, refused past `max` bytes whether declared or streamed. */
export declare function readCapped(resp: Response, max?: number): Promise<ArrayBuffer>;
export declare function readBlobCapped(resp: Response, max?: number): Promise<Blob>;

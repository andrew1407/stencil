// Bounded fetches: a signal that aborts a request after `ms`, surfacing as the runtime's
// own TimeoutError.

export declare const NET_TIMEOUT_MS: number;
export declare const timeoutSignal: (ms?: number) => AbortSignal;

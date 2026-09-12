// Bounded fetches: a signal that aborts a request after `ms`, surfacing as the runtime's
// own AbortError/TimeoutError. Undefined where the platform has neither API.

export declare const NET_TIMEOUT_MS: number;
export declare const timeoutSignal: (ms?: number) => AbortSignal | undefined;

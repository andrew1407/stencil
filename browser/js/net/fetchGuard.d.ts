/** The schemes a guarded fetch will issue: http(s), plus the two local-byte URLs. */
export declare const FETCHABLE_SCHEMES: string[];

/** True when a URL's scheme is in FETCHABLE_SCHEMES. */
export declare function isFetchable(url: unknown): boolean;

/** Scheme-checked fetch, bounded by NET_TIMEOUT_MS unless `init` carries its own signal. */
export declare function guardedFetch(url: string, init?: RequestInit): Promise<Response>;

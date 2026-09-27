// Per-surface LLM glue (llm-contract.md §5): the one module the SHARED client.js is
// allowed to differ through — the surface's own wording and its default stencil-server
// token resolver (the browser app always injects serverBearerToken, so this resolves none).

export declare const ASSISTANT_OFF_TEXT: string;
/** Default `getToken` for createLlmClient when none is injected: no token. */
export declare const defaultGetToken: () => (serverUrl: string) => string;
/** The surface's one loopback classifier (net/urlRules.js), where a key may ride plain http. */
export declare function isLoopbackHost(host: string): boolean;

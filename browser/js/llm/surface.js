// ── Per-surface LLM glue (llm-contract.md §5) ──────────────────────────
// The one module the SHARED client.js is allowed to differ through: the
// surface's own wording and its default stencil-server token resolver. The
// browser points the off-message at the chat panel's settings; the app glue
// always injects getToken (serverBearerToken), so the default resolves none.

export const ASSISTANT_OFF_TEXT = 'The assistant is turned off — choose a provider in the settings to enable it';

// Default `getToken` when createLlmClient gets none injected: no token.
export const defaultGetToken = () => () => '';

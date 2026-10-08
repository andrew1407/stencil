// ── Per-surface LLM glue (llm-contract.md §5) ──────────────────────────
// The one module the SHARED client.js is allowed to differ through: the surface's own wording,
// its loopback classifier and its default stencil-server token resolver (none here).

export { isLoopbackHost } from '../net/urlRules.js';

export const ASSISTANT_OFF_TEXT = 'The assistant is turned off — choose a provider in the settings to enable it';

export const defaultGetToken = () => () => '';

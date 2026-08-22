// ── Per-surface LLM glue (llm-contract.md §5 + §8) ─────────────────────
// The one module the SHARED llmClient.js is allowed to differ through: the
// surface's own wording and its default stencil-server token resolver — plus
// the extension-only helpers that used to live in llmClient.js (they have no
// browser twin, so they ride here to keep the client byte-identical).
//
// stencil-server auth: the extension already stores server connections (URL +
// bearer token, lib/connections.js) — serverTokenFor prefers the stored token
// for the configured serverUrl and falls back to llmSettings.serverToken.
import { loadConnections, connectionByUrl } from '../lib/connections.js';
import { LlmError, PROVIDER_LABELS } from './llmClient.js';

export const ASSISTANT_OFF_TEXT = 'The assistant is turned off — choose a provider in the extension options to enable it';

// Pure: the bearer token for a stencil-server call — the stored connection's token
// for that server (the extension's existing auth), else the explicit settings token.
export const serverTokenFor = (serverUrl, { connections = [], settings = {} } = {}) => {
  const conn = connectionByUrl(connections, serverUrl);
  return (conn && conn.token) || settings.serverToken || '';
};

// Default `getToken` when createLlmClient gets none injected: read the stored
// connection list, then serverTokenFor above.
export const defaultGetToken = (settings) => async (serverUrl) =>
  serverTokenFor(serverUrl, { connections: await loadConnections(), settings });

// The text a failed turn shows — browser unreachableText parity, so the same
// error reads the same on every surface. The endpoint is a LABEL on the
// provider's reason, not a second sentence around it (contract §6.3). An
// endpoint that ANSWERED (err.answered) is quoted in its own words; a tagged
// network failure reads as unreachable. A bare TypeError is NOT assumed to be
// one — plan execution can throw those too. Pure.
export const turnFailureText = (settings, err) => {
  const name = PROVIDER_LABELS[settings?.provider] || settings?.provider || 'the assistant';
  const url = (settings?.provider === 'stencil-server' ? settings?.serverUrl : settings?.baseUrl) || '';
  const at = url ? ` at ${url.replace(/^https?:\/\//i, '')}` : '';
  const why = err?.message ?? String(err ?? '');
  if (err instanceof LlmError && err.answered) return `${name}${at}: ${why}`;
  if (err instanceof LlmError && (err.kind === 'http' || err.kind === 'network')) {
    return `Couldn't reach ${name}${at} — is it running? (${why})`;
  }
  return `Failed: ${why}`;
};

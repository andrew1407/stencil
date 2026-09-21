// ── Per-surface LLM glue (llm-contract.md §5 + §8) ─────────────────────
// The one module the SHARED llmClient.js is allowed to differ through: this surface's own
// wording, its stencil-server token resolver (stored connections first, lib/connections.js)
// and the extension-only helpers with no browser twin — they ride here to keep the client
// byte-identical.
import { loadConnections, connectionByUrl } from '../lib/connection/connections.js';
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

// Browser unreachableText parity. The endpoint is a LABEL on the provider's reason, not a
// second sentence around it (contract §6.3); a bare TypeError is NOT assumed to be network.
export const turnFailureText = (settings, err) => {
  const name = PROVIDER_LABELS[settings?.provider] || settings?.provider || 'the assistant';
  const url = (settings?.provider === 'stencil-server' ? settings?.serverUrl : settings?.baseUrl) || '';
  const at = url ? ` at ${url.replace(/^https?:\/\//i, '')}` : '';
  const why = err?.message ?? String(err ?? '');
  if (err instanceof LlmError && err.answered) return `${name}${at}: ${why}`;
  if (err instanceof LlmError && (err.kind === 'http' || err.kind === 'network')) {
    return `Couldn't reach ${name}${at} (${why})`;
  }
  return `Failed: ${why}`;
};

// The PROVIDER itself unreachable or unconfigured — the 'unreachable' kind of browser
// describeChatError. Drives the Configure-provider CTA (browser chatConfigureButton parity).
export const isUnreachableError = (err) =>
  err instanceof LlmError && (err.kind === 'http' || err.kind === 'network' || err.kind === 'config');

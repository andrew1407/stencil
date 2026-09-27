// ── Per-surface LLM glue (llm-contract.md §5 + §8) ─────────────────────
// The one module the SHARED client.js is allowed to differ through: this surface's own
// wording, its loopback classifier, its stencil-server token resolver (stored connections first,
// lib/connection/connections.js) and the extension-only helpers with no browser twin — they ride here to
// keep the client byte-identical.
import { loadConnections, connectionByUrl } from '../lib/connection/connections.js';
import { LlmError, NO_KEY_TEXT, PROVIDER_LABELS, providerUrl } from './client.js';

export { isLoopbackHost } from '../lib/connection/connections.js';

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
  const url = providerUrl(settings);
  const at = url ? ` at ${url.replace(/^https?:\/\//i, '')}` : '';
  const why = err?.message ?? String(err ?? '');
  if (isMissingSessionKey(err, settings)) {
    return `${name}: ${why}${err.message === NO_KEY_TEXT ? ' — enter your key in the extension options' : ''}.`;
  }
  if (err instanceof LlmError && err.answered) return `${name}${at}: ${why}`;
  if (err instanceof LlmError && (err.kind === 'http' || err.kind === 'network')) {
    return `Couldn't reach ${name}${at} (${why})`;
  }
  return `Failed: ${why}`;
};

// No anthropic session key (never entered, expired or forgotten): the options must ask for it again.
export const isMissingSessionKey = (err, settings) =>
  err instanceof LlmError && err.kind === 'disabled' && settings?.provider === 'anthropic';

// The PROVIDER itself unreachable or unconfigured — the 'unreachable' kind of browser
// describeChatError. Drives the Configure-provider CTA (browser chatConfigureButton parity).
export const isUnreachableError = (err, settings) => isMissingSessionKey(err, settings)
  || (err instanceof LlmError && (err.kind === 'http' || err.kind === 'network' || err.kind === 'config'));

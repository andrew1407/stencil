// What the user reads when a turn lands or fails (llm-contract.md §6.3): the reply plus its
// warnings, and the one table that maps a failed turn to a kind, its text and its card. Pure.

import { LlmError, PROVIDER_LABELS } from './llmClient.js';
import { isAuthStatus } from '../net/connectionManager.js';

// The visible answer for a finished turn: the reply plus any unknown-op skips
// appended in parentheses (contract §1). Pure.
export const replyWithWarnings = (entry) => {
  const reply = entry?.reply ?? '';
  const warnings = entry?.warnings || [];
  return warnings.length ? `${reply}\n(${warnings.join('; ')})` : reply;
};

// A blank completion (a local model whose context the prompt overran) parses as a chat-only
// turn with an empty reply — say so instead of rendering a blank bubble.
export const EMPTY_REPLY_TEXT = 'The model returned an empty answer — nothing was changed. Retry, or switch to a larger model.';
export const settledReplyText = (entry) => replyWithWarnings(entry).trim() || EMPTY_REPLY_TEXT;

// An endpoint that ANSWERED with an error (err.answered) is quoted in its own words: the
// endpoint is a label on that reason, not a second sentence (§6.3 "Say the reason once").
export const unreachableText = (settings, err) => {
  if (settings?.provider === 'none') return 'The assistant is turned off — choose a provider to enable it.';
  const name = PROVIDER_LABELS[settings?.provider] || settings?.provider || 'the assistant';
  const url = (settings?.provider === 'stencil-server' ? settings?.serverUrl : settings?.baseUrl) || '';
  const at = url ? ` at ${url.replace(/^https?:\/\//i, '')}` : '';
  const why = err?.message ?? String(err ?? '');
  if (err?.answered) return `${name}${at}: ${why}`;
  return `Couldn't reach ${name}${at} (${why})`;
};

// `kind`: abort → "Stopped." (no toast); refusal/notice are textual, they ARE the answer;
// expired → card + reconnect; unreachable → card + configure; error → retry. Pure.
export const describeChatError = (err, settings) => {
  if (err?.name === 'AbortError') return { kind: 'abort', text: 'Stopped.' };
  const k = err instanceof LlmError ? err.kind : null;
  if (k === 'refusal') return { kind: 'refusal', text: `Refused: ${err.message}` };
  if (k === 'truncated' || k === 'disabled') return { kind: 'notice', text: err.message };
  // A collaboration server that REFUSED the bearer token gets its own kind, so the card offers
  // the one thing that helps (reconnect) rather than "configure".
  if (settings?.provider === 'stencil-server' && isAuthStatus(err?.status)) {
    const url = settings.serverUrl || '';
    return {
      kind: 'expired',
      text: `Your session on ${url.replace(/^https?:\/\//i, '') || 'the server'} has expired — `
        + 'reconnect to that server, then send this again.',
      serverUrl: url,
    };
  }
  // 'network' is tagged by the client at the fetch itself; a bare TypeError is NOT assumed to
  // be one — plan execution throws those too (canvas APIs).
  if (k === 'http' || k === 'config' || k === 'network') {
    return { kind: 'unreachable', text: unreachableText(settings, err) };
  }
  return { kind: 'error', text: `Error: ${err?.message ?? err}` };
};

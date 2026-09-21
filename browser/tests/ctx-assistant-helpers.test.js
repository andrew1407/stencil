// Pure helpers both chat surfaces share (js/llm/chatSession.js): replyWithWarnings,
// unreachableText, describeChatError and runChatTurn's rejected-turn result.
import { test } from 'node:test';
import assert from 'node:assert';
import {
  replyWithWarnings, unreachableText, describeChatError, runChatTurn, chatLog, resetChatLog,
  runLoggedChatTurn,
} from '../js/llm/chat/chatSession.js';
import { LlmError } from '../js/llm/llmClient.js';
import { chatViewSource } from './helpers/chatViewSource.js';

// ── Pure helpers shared by both chat surfaces ──
test('replyWithWarnings appends unknown-op skips to the visible reply', () => {
  assert.strictEqual(replyWithWarnings({ reply: 'Done.', warnings: [] }), 'Done.');
  assert.strictEqual(replyWithWarnings({ reply: 'Done.', warnings: ['skipped op "zoom"'] }), 'Done.\n(skipped op "zoom")');
  assert.strictEqual(replyWithWarnings({ reply: 'Done.', warnings: ['a', 'b'] }), 'Done.\n(a; b)');
  assert.strictEqual(replyWithWarnings({ reply: 'Hi' }), 'Hi');
  assert.strictEqual(replyWithWarnings(null), '');
});

// Desktop parity: success notes ("opened cat.jpg in the editor first…") ride the
// reply as neutral text — ONE assistant bubble, never the error style.
test('a successful turn with warnings stays one NON-error assistant row', async () => {
  resetChatLog();
  const controller = {
    attachments: [],
    async send() {
      return { reply: 'Done.', warnings: ['opened cat.jpg in the editor first — the actions ran on it'], results: [] };
    },
  };
  await runLoggedChatTurn(controller, 'make it sepia');
  const rows = chatLog();
  assert.strictEqual(rows.length, 2, 'one user row + one assistant row, nothing extra');
  const assistantRow = rows[1];
  assert.strictEqual(assistantRow.role, 'assistant');
  assert.ok(assistantRow.text.includes('opened cat.jpg in the editor first'), 'the note rides the reply');
  assert.ok(!assistantRow.error, 'a warning is not an error — the row must stay neutral');
  resetChatLog();
  // And the view applies the red style only off that flag — never off warnings.
  const view = chatViewSource();
  assert.ok(view.includes("(row.error ? ' chat-msg-error' : '')"), 'chat-msg-error is gated on row.error alone');
});

test('unreachableText names the provider and its endpoint (host only)', () => {
  assert.strictEqual(
    unreachableText({ provider: 'ollama', baseUrl: 'http://localhost:11434' }, new Error('fetch failed')),
    "Couldn't reach Ollama at localhost:11434 (fetch failed)");
  assert.strictEqual(
    unreachableText({ provider: 'stencil-server', serverUrl: 'https://srv:8090', baseUrl: 'ignored' }, new Error('401')),
    "Couldn't reach Stencil server at srv:8090 (401)");
  assert.strictEqual(
    unreachableText({ provider: 'none' }, new Error('x')),
    'The assistant is turned off — choose a provider to enable it.');
  assert.ok(unreachableText({ provider: 'openai-compat', baseUrl: '' }, new Error('boom')).startsWith("Couldn't reach"));
});

test('describeChatError maps a failed turn to what both surfaces render', () => {
  const s = { provider: 'ollama', baseUrl: 'http://localhost:11434' };
  const abort = new Error('aborted');
  abort.name = 'AbortError';
  assert.deepStrictEqual(describeChatError(abort, s), { kind: 'abort', text: 'Stopped.' });
  assert.deepStrictEqual(describeChatError(new LlmError('policy', 'refusal'), s), { kind: 'refusal', text: 'Refused: policy' });
  assert.deepStrictEqual(describeChatError(new LlmError('reply was cut off', 'truncated'), s),
    { kind: 'notice', text: 'reply was cut off' });
  assert.deepStrictEqual(describeChatError(new LlmError('no key', 'disabled'), s), { kind: 'notice', text: 'no key' });
  assert.strictEqual(describeChatError(new LlmError('500', 'http'), s).kind, 'unreachable');
  assert.strictEqual(describeChatError(new LlmError('bad config', 'config'), s).kind, 'unreachable');
  // fetch failures arrive pre-tagged by the client ('network'); a BARE TypeError
  // is not assumed to be one — canvas APIs in plan execution throw those too.
  assert.strictEqual(describeChatError(new LlmError('Failed to fetch', 'network'), s).kind, 'unreachable');
  assert.strictEqual(describeChatError(new TypeError("Failed to execute 'drawImage'"), s).kind, 'error');
  // A plain Error (e.g. an invalid plan) is textual, prefixed.
  assert.deepStrictEqual(describeChatError(new Error('action 2 invalid'), s),
    { kind: 'error', text: 'Error: action 2 invalid' });
});

test('runChatTurn surfaces a rejected turn instead of throwing', async () => {
  const boom = new LlmError('down', 'http');
  const ctrl = { send: async () => { throw boom; } };
  const res = await runChatTurn(ctrl, 'hi', { settings: { provider: 'ollama', baseUrl: 'http://localhost:11434' } });
  assert.strictEqual(res.ok, false);
  assert.strictEqual(res.kind, 'unreachable');
  assert.strictEqual(res.error, boom, 'the original error rides along (the panel rethrows it)');
  assert.ok(res.text.includes('localhost:11434'));
});

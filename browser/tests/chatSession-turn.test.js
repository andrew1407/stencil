// The logged-turn frame (js/llm/chatSession.js): the error cards, Stop's AbortController, the
// hook order, the never-left-spinning guard and queueAttachments. From chatSession.test.js.
import { test } from 'node:test';
import assert from 'node:assert';

import {
  chatLog, resetChatLog, chatTurnInFlight,
  runLoggedChatTurn, unreachableText, queueAttachments, ATTACHMENT_CAP_NOTICE,
} from '../js/llm/chat/chatSession.js';
import { LlmError } from '../js/llm/llmClient.js';

// ── The logged-turn frame: error paths, Stop, and the spinning-row guard ────
const lastRow = () => chatLog().at(-1);

test('a typed badReply (malformed 200) settles as a plain retryable error, no card', async () => {
  resetChatLog();
  const controller = { send: async () => { throw new LlmError('malformed ollama response (no message.content)', 'badReply'); } };
  const res = await runLoggedChatTurn(controller, 'hi', { settings: { provider: 'ollama', baseUrl: 'http://x' } });
  assert.strictEqual(res.kind, 'error', 'badReply is not "unreachable" — the endpoint answered');
  const row = lastRow();
  assert.strictEqual(row.text, 'Error: malformed ollama response (no message.content)');
  assert.strictEqual(row.error, true);
  assert.strictEqual(row.card, false, 'no configure-provider card for a model-side failure');
  assert.strictEqual(row.retryText, 'hi', 'retry re-sends the same prompt');
  resetChatLog();
});

test('a server with no LLM key (kind "disabled") is a textual notice in its own words', async () => {
  resetChatLog();
  const controller = { send: async () => { throw new LlmError('LLM is not enabled on this server', 'disabled'); } };
  const res = await runLoggedChatTurn(controller, 'hi', { settings: { provider: 'stencil-server', serverUrl: 'http://s' } });
  assert.strictEqual(res.kind, 'notice');
  assert.strictEqual(lastRow().text, 'LLM is not enabled on this server', 'the typed message IS the answer');
  assert.strictEqual(lastRow().card, false);
  resetChatLog();
});

test('a transport failure renders the unreachable card with the provider named', async () => {
  resetChatLog();
  const settings = { provider: 'ollama', baseUrl: 'http://localhost:11434' };
  const controller = { send: async () => { throw new LlmError('fetch failed', 'network'); } };
  const res = await runLoggedChatTurn(controller, 'hi', { settings });
  assert.strictEqual(res.kind, 'unreachable');
  const row = lastRow();
  assert.strictEqual(row.card, true, 'card + configure CTA');
  assert.strictEqual(row.reconnect, null, 'nothing to reconnect to — the config is the cure');
  assert.strictEqual(row.text, unreachableText(settings, res.error));
  resetChatLog();
});

test('an expired stencil-server session patches the row with the reconnect URL', async () => {
  resetChatLog();
  const err = Object.assign(new LlmError('unauthorized', 'http'), { status: 401, answered: true });
  const controller = { send: async () => { throw err; } };
  const res = await runLoggedChatTurn(controller, 'hi',
    { settings: { provider: 'stencil-server', serverUrl: 'http://localhost:8090' } });
  assert.strictEqual(res.kind, 'expired');
  const row = lastRow();
  assert.strictEqual(row.card, true);
  assert.strictEqual(row.reconnect, 'http://localhost:8090', 'the card knows WHICH server to sign back into');
  assert.match(row.text, /session on localhost:8090 has expired/);
  resetChatLog();
});

test('Stop: begin hands out the AbortController whose signal the send received', async () => {
  resetChatLog();
  let seenSignal = null;
  const controller = {
    send: (text, { signal }) => new Promise((_, reject) => {
      seenSignal = signal;
      signal.addEventListener('abort', () => {
        const e = new Error('aborted'); e.name = 'AbortError'; reject(e);
      });
    }),
  };
  let abort = null;
  const p = runLoggedChatTurn(controller, 'outline it', { begin: (a) => { abort = a; } });
  assert.ok(abort instanceof AbortController, 'begin fires before the send, with the live controller');
  abort.abort();
  const res = await p;
  assert.strictEqual(seenSignal, abort.signal, 'one AbortController end to end');
  assert.strictEqual(res.kind, 'abort');
  assert.strictEqual(lastRow().text, 'Stopped.');
  assert.strictEqual(lastRow().retryText, 'outline it', 'a change of mind keeps its Retry');
  resetChatLog();
});

test('hooks run begin → onResult → cleanup, with the flag down before cleanup', async () => {
  resetChatLog();
  const order = [];
  const controller = {
    send: async () => { order.push(`send inFlight=${chatTurnInFlight()}`); return { reply: 'ok', warnings: [], results: [] }; },
  };
  const res = await runLoggedChatTurn(controller, 'hi', {
    begin: () => order.push('begin'),
    onResult: (r) => order.push(`onResult ok=${r.ok}`),
    cleanup: () => order.push(`cleanup inFlight=${chatTurnInFlight()}`),
  });
  assert.strictEqual(res.ok, true);
  // The flag is up during the send and DOWN before cleanup — a surface may send again
  // from its cleanup (Resend), and the guard must not see its own previous turn.
  assert.deepStrictEqual(order, ['begin', 'send inFlight=true', 'onResult ok=true', 'cleanup inFlight=false']);
  resetChatLog();
});

test('no row is ever left spinning: a patch failure still settles the pending row', async () => {
  resetChatLog();
  // A pathological entry (undefined) makes the success patch itself throw — the
  // belt-and-braces finally must still resolve the "…" row into a retryable error.
  const controller = { send: async () => undefined };
  let cleaned = false;
  await assert.rejects(
    runLoggedChatTurn(controller, 'hi', { cleanup: () => { cleaned = true; } }),
    TypeError);
  const row = lastRow();
  assert.strictEqual(row.pending, false, 'the dots stop');
  assert.strictEqual(row.error, true);
  assert.strictEqual(row.text, 'Error: the turn ended without an answer.');
  assert.strictEqual(row.retryText, 'hi', 'even this dead end keeps its Retry');
  assert.strictEqual(cleaned, true, 'cleanup still ran');
  assert.strictEqual(chatTurnInFlight(), false);
  resetChatLog();
});

test('consecutive logged turns thread one transcript in order', async () => {
  resetChatLog();
  let n = 0;
  const controller = { send: async () => ({ reply: `reply ${++n}`, warnings: [], results: [] }) };
  await runLoggedChatTurn(controller, 'first');
  await runLoggedChatTurn(controller, 'second');
  assert.deepStrictEqual(chatLog().map((r) => `${r.role}:${r.text}`),
    ['user:first', 'assistant:reply 1', 'user:second', 'assistant:reply 2']);
  const ids = chatLog().map((r) => r.id);
  assert.deepStrictEqual([...ids].sort((a, b) => a - b), ids, 'row ids stay monotonic across turns');
  resetChatLog();
});

// ── unreachableText fallbacks (the named-provider cases live in ctx-assistant) ──
test('unreachableText degrades gracefully without a label or a URL', () => {
  // An unknown provider id is shown as itself; no provider at all → "the assistant".
  assert.ok(unreachableText({ provider: 'mystery', baseUrl: 'http://h' }, new Error('x'))
    .startsWith("Couldn't reach mystery at h"));
  assert.strictEqual(unreachableText(null, new Error('x')),
    "Couldn't reach the assistant (x)");
  // No URL → no dangling " at ", and a bare non-Error reason still reads.
  assert.strictEqual(unreachableText({ provider: 'openai-compat', baseUrl: '' }, 'ECONNREFUSED'),
    "Couldn't reach OpenAI API (LM Studio, vLLM, …) (ECONNREFUSED)");
});

// Files past the §7 cap are a NOTICE, said once per batch rather than one failure toast per file; a
// genuinely bad file still reports as a failure.
test('queueAttachments counts the files past the cap and reports them once, apart from real failures', async () => {
  const attachments = [{}, {}];
  const controller = {
    attachments,
    async addAttachment(file) {
      if (file.type !== 'image/png') throw new Error(`Not an image or video (got "${file.type}")`);
      attachments.push({ name: file.name });
    },
  };
  const errors = [], capped = [];
  const files = [
    { name: 'bad.txt', type: 'text/plain' },
    { name: 'three.png', type: 'image/png' },
    { name: 'four.png', type: 'image/png' },
    { name: 'five.png', type: 'image/png' },
  ];
  const added = await queueAttachments(controller, files, (err, f) => errors.push(f.name), (n) => capped.push(n));
  assert.equal(added, 1);
  assert.deepEqual(errors, ['bad.txt'], 'only the unreadable file is a failure');
  assert.deepEqual(capped, [2], 'the two files past the cap are reported together, once');
  assert.equal(attachments.length, 3);
  assert.match(ATTACHMENT_CAP_NOTICE, /^Up to 3 images per message/);
  // Under the cap nothing is reported at all.
  const quiet = [];
  await queueAttachments({ attachments: [], addAttachment: async () => {} }, [files[1]], () => quiet.push('err'), () => quiet.push('cap'));
  assert.deepEqual(quiet, []);
});

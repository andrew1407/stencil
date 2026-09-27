// Menu-open guards (js/ui/contextMenu/contextMenu.js): what keeps the menu open while chatting, the
// answered-endpoint wording, the Retry a stop leaves, and the shared closed-turn toast.
import { test } from 'node:test';
import assert from 'node:assert';
import {
  unreachableText, describeChatError, chatLog, resetChatLog, runLoggedChatTurn,
} from '../../../js/llm/chat/session.js';
import { LlmError } from '../../../js/llm/client.js';
import { wireContextMenu, typeAndSend } from '../../helpers/ctxMenuChatRig.js';
import { wireBothSurfaces } from '../../helpers/chatSurfacesRig.js';

// ── Menu-open guards (the behaviour that makes chatting in a menu possible) ──
const tick = () => new Promise((r) => setTimeout(r, 0));

test('contextMenu.js keeps the menu open while chatting', async () => {
  const m = await wireContextMenu();
  m.openAt();
  assert.ok(m.isOpen());
  // Scrolling the transcript (inside the menu) is not a page scroll.
  m.doc.fire('scroll', { target: m.flyout.transcript });
  assert.ok(m.isOpen(), 'a scroll inside the menu leaves it open');
  // A press inside never reaches the document closer; the menu swallows its own mousedown.
  m.doc.fire('mousedown', { target: m.flyout.input });
  assert.ok(m.isOpen(), 'a press inside the menu leaves it open');
  assert.ok(m.menu.fire('mousedown').stopped, 'the menu stops its own mousedown');
  // A plan re-laying out the canvas scrolls the viewport: exempt while the turn runs…
  typeAndSend(m.flyout, 'crop to the cat');
  assert.deepStrictEqual(m.ctrl.sent, ['crop to the cat']);
  m.doc.fire('scroll', { target: m.doc });
  assert.ok(m.isOpen(), 'the running turn\'s scroll is the assistant\'s');
  m.ctrl.settle({ reply: 'Cropped.', results: [] });
  await tick();
  // …and for a grace window after it, while its last relayout settles.
  m.doc.fire('scroll', { target: m.doc });
  assert.ok(m.isOpen(), 'the grace window still covers a late relayout');
  const now = Date.now;
  Date.now = () => now() + 5000;
  try { m.doc.fire('scroll', { target: m.doc }); } finally { Date.now = now; }
  assert.strictEqual(m.isOpen(), false, 'a user\'s page scroll past the grace closes it');
  m.openAt();
  m.doc.fire('mousedown', { target: m.doc.getElementById('canvas') });
  assert.strictEqual(m.isOpen(), false, 'a press outside closes it');
});

test('unreachableText quotes an endpoint that ANSWERED instead of guessing it is down', () => {
  const err = new Error("model 'qwen2.5:0.5b' not found, try pulling it first");
  err.answered = true;
  assert.strictEqual(
    unreachableText({ provider: 'ollama', baseUrl: 'http://localhost:11434' }, err),
    "Ollama at localhost:11434: model 'qwen2.5:0.5b' not found, try pulling it first");
});

// The endpoint is a LABEL on the reason, never a second sentence around it: the
// server says the reason once (contract §6.3) and the card must not restate it.
test('an answered endpoint adds the host and nothing else', () => {
  const err = new LlmError('the LLM provider is out of credits or has no active billing', 'http');
  err.answered = true;
  err.status = 502;
  assert.strictEqual(
    unreachableText({ provider: 'stencil-server', serverUrl: 'http://localhost:8090' }, err),
    'Stencil server at localhost:8090: the LLM provider is out of credits or has no active billing');
  const shown = describeChatError(err, { provider: 'stencil-server', serverUrl: 'http://localhost:8090' });
  assert.strictEqual(shown.kind, 'unreachable');   // keeps the "Configure provider" CTA
  assert.ok(!/answered|HTTP 502|credit balance/.test(shown.text), shown.text);
});

// A stopped turn is a change of mind, not a dead end: the row keeps the prompt so the
// renderer can offer Retry (browser, extension and desktop all do this now).
test('stopping a turn leaves a Retry with the original text', async () => {
  resetChatLog();
  const controller = { send: async () => { const e = new Error('aborted'); e.name = 'AbortError'; throw e; } };
  await runLoggedChatTurn(controller, 'outline the cat', { settings: { provider: 'ollama' } });
  const rows = chatLog();
  const reply = rows[rows.length - 1];
  assert.strictEqual(reply.text, 'Stopped.');
  assert.strictEqual(reply.retryText, 'outline the cat', 'the prompt survives the stop');
  assert.strictEqual(reply.pending, false, 'the pending mark is cleared, so the dots stop');
});

test('the flyout\'s attach-cap check resolves MAX_ATTACHMENTS: a full queue disables attach', async () => {
  const { MAX_ATTACHMENTS } = await import('../../../js/llm/chat/controller.js');
  const m = await wireContextMenu();
  const attach = m.doc.getElementById('ctx-assist-attach-btn');
  assert.strictEqual(attach.disabled, false, 'wiring evaluated the cap check without throwing');
  m.ctrl.attachments.push(...Array.from({ length: MAX_ATTACHMENTS }, (_, i) => ({ name: `a${i}.png` })));
  m.flyout.input.fire('input');
  assert.strictEqual(attach.disabled, true, 'a full queue disables attach');
  m.ctrl.attachments.pop();
  m.flyout.input.fire('input');
  assert.strictEqual(attach.disabled, false);
});

// The closed-chat balloon is built once for every surface: the flyout's toast frames the
// outcome and carries a click action, exactly as the panel's does.
test('closedTurnToast: one framing for both surfaces, and silence for an abort', async () => {
  const { closedTurnToast, CHAT_TOAST_CHARS, EMPTY_REPLY_TEXT } = await import('../../../js/llm/chat/session.js');
  assert.deepStrictEqual(closedTurnToast({ ok: true, entry: { reply: 'Cropped it.', results: [] } }),
    { text: 'Assistant finished — Cropped it.', type: 'ok' });
  // The image count rides the framing, pluralised.
  assert.strictEqual(closedTurnToast({ ok: true, entry: { reply: 'Two ways.', results: [1, 2] } }).text,
    'Assistant finished (2 images) — Two ways.');
  assert.strictEqual(closedTurnToast({ ok: true, entry: { reply: 'One.', results: [1] } }).text,
    'Assistant finished (1 image) — One.');
  // A wordless turn still says something (the transcript's own empty-answer line).
  assert.ok(closedTurnToast({ ok: true, entry: { reply: '', results: [] } }).text.includes(EMPTY_REPLY_TEXT.slice(0, 20)));
  // Failures name the cause; aborts are the user's own doing and say nothing at all.
  assert.deepStrictEqual(closedTurnToast({ ok: false, kind: 'error', error: { message: 'boom' } }),
    { text: 'Assistant failed — boom', type: 'fail' });
  assert.strictEqual(closedTurnToast({ ok: false, kind: 'abort', text: 'Stopped.' }), null);
  assert.strictEqual(closedTurnToast(null), null);
  // …and it is truncated for the balloon, wherever it came from.
  const long = closedTurnToast({ ok: true, entry: { reply: 'x'.repeat(400), results: [] } });
  assert.strictEqual(long.text.length, CHAT_TOAST_CHARS);
  assert.ok(long.text.endsWith('…'));
});

test('both surfaces toast through the shared builder, each with a way back to the chat', async () => {
  const { closedTurnToast } = await import('../../../js/llm/chat/session.js');
  const s = await wireBothSurfaces();
  const entry = { reply: 'x'.repeat(300), results: [1] };
  const expected = closedTurnToast({ ok: true, entry });
  // The panel, closed: the landed turn toasts, and the click reopens the panel itself.
  typeAndSend(s.panel, 'outline it');
  s.ctrl.settle(entry);
  await tick();
  assert.deepStrictEqual(s.notices.map((n) => [n.text, n.type]), [[expected.text, expected.type]],
    'the panel frames its toast with the shared builder, truncation included');
  s.notices[0].opts.onClick();
  assert.ok(s.panel.host.classList.contains('chat-open'), 'the panel toast reopens the panel');
  typeAndSend(s.panel, 'again');
  s.ctrl.settle(entry);
  await tick();
  assert.strictEqual(s.notices.length, 1, 'an open panel says nothing');
  // The flyout, its menu closed: the same balloon, and the click opens the docked panel.
  const opened = [];
  s.app.chat.open = () => opened.push('panel');
  s.host.open = false;
  typeAndSend(s.flyout, 'outline it');
  s.ctrl.settle(entry);
  await tick();
  assert.deepStrictEqual([s.notices[1].text, s.notices[1].type], [expected.text, expected.type]);
  s.notices[1].opts.onClick();
  assert.deepStrictEqual(opened, ['panel'], 'the flyout cannot restore itself, so it opens the panel');
  s.host.open = true;
  typeAndSend(s.flyout, 'again');
  s.ctrl.settle(entry);
  await tick();
  assert.strictEqual(s.notices.length, 2, 'an open menu says nothing');
});

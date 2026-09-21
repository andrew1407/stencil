// Menu-open guards (js/ui/contextMenu.js): what keeps the menu open while chatting, the
// answered-endpoint wording, the Retry a stop leaves, and the shared closed-turn toast.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import {
  unreachableText, describeChatError, chatLog, resetChatLog, runLoggedChatTurn,
} from '../js/llm/chat/chatSession.js';
import { LlmError } from '../js/llm/llmClient.js';
import { contextMenuSource } from './helpers/contextMenuSource.js';

// ── Menu-open guards (the behaviour that makes chatting in a menu possible) ──
test('contextMenu.js keeps the menu open while chatting', () => {
  const src = contextMenuSource();
  // Scroll-close ignores scrolls that originate INSIDE the menu (the transcript).
  assert.match(src, /if \(e\.target && e\.target\.nodeType && menu\.contains\(e\.target\)\) return;/);
  // Inside clicks never reach the document mousedown close handler.
  assert.ok(src.includes("menu.addEventListener('mousedown', e => e.stopPropagation())"));
  // A plan re-laying out the canvas scrolls the viewport — that scroll is the
  // assistant's, not the user's, so it must not dismiss the menu either.
  assert.ok(src.includes('if (assistantBusy()) return;'), 'assistant-caused scrolls are exempt');
  assert.match(src, /const assistantBusy = \(\) => assistSending \|\| Date\.now\(\) < assistBusyUntil;/);
  // The entry is (re)built and re-moded on open and when the provider changes.
  assert.ok(src.includes('subscribe(EVENTS.llmSettingsChanged, syncAssistant)'));
  assert.ok(src.includes('syncAssistant();\n      syncScript();\n      menu.style.left'), 'both flyout entries settled before the menu is measured');
  // Typing in the flyout (or a running turn) suppresses the hover-out close, but
  // hovering a SIBLING parent still closes it like any other flyout.
  assert.ok(src.includes("flyout._keepOpen = () => host.sending() || resizing || chatRowMenuOpen() || flyout.contains(document.activeElement);"),
    'typing, resizing the composer, an open row menu, or a running turn all count as engaged');
  assert.match(src, /const keepSubOpen = \(sub\) => !!sub\._keepOpen\?\.\(\);/);
  assert.ok(src.includes('if (keepSubOpen(sub)) return;'), 'hideSub honours the engaged flyout');
  // The only closeMenu() calls in ctxAssistantChat.js are the four that open something else on
  // top (gear, Configure provider, Reconnect, the phone hand-over); chatting never closes it.
  const assist = readFileSync(new URL('../js/ui/ctx/ctxAssistantChat.js', import.meta.url), 'utf8');
  assert.strictEqual((assist.match(/host\.closeMenu/g) || []).length, 4,
    'gear + settings CTA + reconnect CTA + phone hand-over only');
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

test('every bare identifier contextMenu.js uses from other llm modules is imported', () => {
  const src = contextMenuSource();
  // Regression: attachFull referenced MAX_ATTACHMENTS without importing it (runtime-only crash).
  assert.match(src, /import \{ MAX_ATTACHMENTS \} from '[^']*chatController\.js';/,
    'MAX_ATTACHMENTS is imported where the attach-cap check uses it');
});

// The closed-chat balloon is built once for every surface: the flyout's toast frames the
// outcome and carries a click action, exactly as the panel's does.
test('closedTurnToast: one framing for both surfaces, and silence for an abort', async () => {
  const { closedTurnToast, CHAT_TOAST_CHARS, EMPTY_REPLY_TEXT } = await import('../js/llm/chat/chatSession.js');
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

test('both surfaces toast through the shared builder, each with a way back to the chat', () => {
  const panel = readFileSync(new URL('../js/ui/chat/chatPanel.js', import.meta.url), 'utf8');
  const menu = contextMenuSource();
  for (const [name, src] of [['panel', panel], ['flyout', menu]]) {
    assert.ok(src.includes('closedTurnToast('), `${name} builds its toast from the shared helper`);
    assert.ok(!/truncateForToast\(/.test(src), `${name} no longer frames its own`);
    assert.ok(/notify\(toast\.text, toast\.type, \{ onClick:/.test(src), `${name}'s toast reopens the chat`);
  }
  // The flyout cannot restore itself (it needs the menu at its old point), so it opens
  // the docked panel — the same conversation.
  assert.ok(menu.includes('onClick: () => app.chat?.open()'));
  assert.ok(panel.includes('onClick: () => setOpen(true)'));
});

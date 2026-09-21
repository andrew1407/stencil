// renderChatLog over a live tree (js/llm/session.js): one text node per row however often
// it repaints, a "…" trigger on every settled row, and no row left spinning.
import { test } from 'node:test';
import assert from 'node:assert';
import { resetChatLog, chatLog, runLoggedChatTurn, chatTurnInFlight } from '../../../js/llm/chat/session.js';
import { descendants, makeEl, stubDom, rowsOf, textNodesOf, PROMPT } from '../../helpers/chatTranscriptRig.js';

// ── The reported bug: a bubble showing its prompt twice ─────────────────────


test('a bubble renders its text exactly ONCE, however often the log repaints', async () => {
  stubDom();
  const { renderChatLog } = await import('../../../js/ui/chat/view.js?render-once');
  const transcript = makeEl();
  const log = [
    { id: 1, role: 'user', text: PROMPT },
    { id: 2, role: 'assistant', text: '…', pending: true },
  ];
  const hooks = { onConfigure() {}, onRetry() {} };
  renderChatLog(transcript, log, hooks);
  // The turn fails: the pending row resolves into the unreachable CARD, which is the
  // row that used to be torn down and rebuilt on every single repaint.
  Object.assign(log[1], { pending: false, text: 'Couldn\'t reach it', error: true, card: true, retryText: PROMPT });
  for (let i = 0; i < 5; i++) renderChatLog(transcript, log, hooks);

  const [user, card] = rowsOf(transcript);
  assert.strictEqual(rowsOf(transcript).length, 2, 'two bubbles, not four');
  // ONE text node per row, and it holds the message once — no second copy stacked
  // beside the row's buttons.
  assert.strictEqual(textNodesOf(user).length, 1, 'the user bubble has one text node');
  assert.strictEqual(textNodesOf(card).length, 1);
  assert.strictEqual(user.textContent.split(PROMPT).length - 1, 1, 'the prompt appears exactly once');
  assert.strictEqual(user.textContent, PROMPT, 'and nothing else leaks into the bubble');
  // The affordances are built once too — five repaints must not stack five buttons.
  assert.strictEqual(descendants(card).filter((d) => d.classList.contains('chat-retry-cta')).length, 1);
  assert.strictEqual(descendants(card).filter((d) => d.classList.contains('chat-config-cta')).length, 1);
  // …and the column class the Retry needs survives the wholesale className rewrite.
  assert.ok(card.classList.contains('chat-msg-cta'), 'the retry row stays a flex column');
});

test('the "…" trigger exists on EVERY settled row, the first/oldest included', async () => {
  stubDom();
  const { renderChatLog } = await import('../../../js/ui/chat/view.js?render-menu');
  const transcript = makeEl();
  const log = [
    { id: 1, role: 'user', text: 'first' },
    { id: 2, role: 'assistant', text: 'reply' },
    { id: 3, role: 'user', text: 'second' },
    { id: 4, role: 'assistant', text: '…', pending: true },
  ];
  renderChatLog(transcript, log, {});
  const rows = rowsOf(transcript);
  const trigger = (r) => descendants(r).filter((d) => d.classList.contains('chat-row-menu-btn'));
  assert.deepStrictEqual(rows.slice(0, 3).map((r) => trigger(r).length), [1, 1, 1],
    'the oldest row gets one just like the newest');
  assert.strictEqual(trigger(rows[3]).length, 0, 'an in-flight row has no menu to open');
  // It settles → it gets one, and a repaint never adds a second.
  Object.assign(log[3], { pending: false, text: 'done' });
  renderChatLog(transcript, log, {});
  renderChatLog(transcript, log, {});
  assert.deepStrictEqual(rowsOf(transcript).map((r) => trigger(r).length), [1, 1, 1, 1]);
  // The trigger sits on the corner facing the panel centre, per role.
  assert.ok(trigger(rows[0])[0].classList.contains('chat-row-menu-btn-left'));
  assert.ok(trigger(rows[1])[0].classList.contains('chat-row-menu-btn-right'));
});

test('a FAILED turn — error card or Stop — gets the "…" too, beside its own Retry', async () => {
  stubDom();
  const { renderChatLog } = await import('../../../js/ui/chat/view.js?render-menu-error');
  const transcript = makeEl();
  const log = [
    { id: 1, role: 'user', text: 'crop to portrait' },
    // The reported card: unreachable provider → message + configure CTA + Retry.
    { id: 2, role: 'assistant', text: 'Ollama at localhost:11434 answered: model is required',
      error: true, card: true, retryText: 'crop to portrait' },
    { id: 3, role: 'assistant', text: 'Stopped.', error: true, retryText: 'crop to portrait' },
  ];
  const hooks = { onConfigure() {}, onRetry() {} };
  renderChatLog(transcript, log, hooks);
  renderChatLog(transcript, log, hooks);   // a repaint must not stack a second one
  const rows = rowsOf(transcript);
  const has = (r, cls) => descendants(r).filter((d) => d.classList.contains(cls));
  assert.deepStrictEqual(rows.map((r) => has(r, 'chat-row-menu-btn').length), [1, 1, 1],
    'the error card and the stopped row are settled rows like any other');
  // Assistant side, so the trigger faces the panel centre from the RIGHT.
  assert.ok(has(rows[1], 'chat-row-menu-btn')[0].classList.contains('chat-row-menu-btn-right'));
  assert.ok(has(rows[2], 'chat-row-menu-btn')[0].classList.contains('chat-row-menu-btn-right'));
  // …and the card's own controls are still there beside it (the menu replaces nothing).
  assert.strictEqual(has(rows[1], 'chat-retry-cta').length, 1);
  assert.strictEqual(has(rows[1], 'chat-config-cta').length, 1);
  assert.strictEqual(has(rows[2], 'chat-retry-cta').length, 1, 'a Stop is retryable too');
  assert.strictEqual(has(rows[2], 'chat-config-cta').length, 0, 'but it is no provider card');
});

// The typing dots are three EMPTY <i>s, so a pending bubble's text reads '' and a
// write-only-when-it-changed settle never fires for an empty reply (user report).
test('a turn that settles with an EMPTY reply still drops its typing indicator', async () => {
  stubDom();
  const { renderChatLog } = await import('../../../js/ui/chat/view.js?render-empty-settle');
  const transcript = makeEl();
  const log = [{ id: 1, role: 'assistant', text: '…', pending: true }];
  renderChatLog(transcript, log, {});
  const row = rowsOf(transcript)[0];
  const dots = () => descendants(row).filter((d) => d.classList.contains('chat-typing')).length;
  assert.strictEqual(dots(), 1, 'an in-flight turn spins');
  // It settles with NOTHING to say — the exact shape a blank model answer produces.
  Object.assign(log[0], { pending: false, text: '' });
  renderChatLog(transcript, log, {});
  assert.strictEqual(dots(), 0, 'the settled row stopped spinning');
  assert.strictEqual(row.textContent, '', 'and holds its (empty) reply');
  // …and the menu the row now deserves is there, since it is a settled row.
  assert.strictEqual(descendants(row).filter((d) => d.classList.contains('chat-row-menu-btn')).length, 1);
});

test('a blank model answer becomes WORDS, not an empty bubble', async () => {
  const { settledReplyText, EMPTY_REPLY_TEXT, replyWithWarnings } =
    await import('../../../js/llm/chat/session.js');
  // Nothing at all to show → say so; anything else is passed straight through.
  assert.strictEqual(settledReplyText({ reply: '', warnings: [] }), EMPTY_REPLY_TEXT);
  assert.strictEqual(settledReplyText({ reply: '   ' }), EMPTY_REPLY_TEXT);
  assert.strictEqual(settledReplyText(null), EMPTY_REPLY_TEXT);
  assert.strictEqual(settledReplyText({ reply: 'Done.' }), 'Done.');
  // A warning alone is still an answer — shown without the empty reply's blank line.
  assert.strictEqual(settledReplyText({ reply: '', warnings: ['loaded it'] }), '(loaded it)');
  assert.strictEqual(replyWithWarnings({ reply: '', warnings: ['loaded it'] }), '\n(loaded it)');
});

test('nothing can leave a row pending: the turn settles it in a finally', async () => {
  resetChatLog();
  // The answer arrives, then the surface's own onResult throws — the row must not be
  // left spinning by someone else's bug.
  const controller = { attachments: [], async send() { return { reply: 'Done.', warnings: [] }; } };
  await assert.rejects(() => runLoggedChatTurn(controller, 'go', {
    onResult() { throw new Error('surface blew up'); },
  }));
  const rows = chatLog();
  assert.strictEqual(rows.at(-1).pending, false, 'the row is settled whatever happened');
  assert.strictEqual(chatTurnInFlight(), false, 'and the shared flag is clear');
  resetChatLog();
});

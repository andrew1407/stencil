// Retry and the turn's end (js/llm/chat/session.js): one turn per click logged once, the shared
// in-flight flag both surfaces read, and a settled reply carrying no progress line.
import { test } from 'node:test';
import assert from 'node:assert';
import { resetChatLog, chatLog, runLoggedChatTurn, chatTurnInFlight } from '../../../js/llm/chat/session.js';
import { COMPONENTS_CSS } from '../../helpers/css.js';
import { descendants, makeEl, stubDom, rowsOf, PROMPT } from '../../helpers/chatTranscriptRig.js';
import { wireBothSurfaces, typeAndSend, rowEl, rightClick, openRowMenu, clickRowItem } from '../../helpers/chatSurfacesRig.js';

// ── Retry: one turn per click, logged once ──────────────────────────────────
const failingController = () => ({
  attachments: [],
  sent: [],
  async send(text) { this.sent.push(text); throw new Error('LLM request failed'); },
});

test('retrying a failed turn logs a NEW turn — it never doubles an existing bubble', async () => {
  resetChatLog();
  const controller = failingController();
  await runLoggedChatTurn(controller, PROMPT);
  await runLoggedChatTurn(controller, PROMPT);   // the Retry button's exact path
  const rows = chatLog();
  assert.deepStrictEqual(rows.map((r) => r.role), ['user', 'assistant', 'user', 'assistant']);
  // Each user row carries the prompt ONCE — no retry appended it to the row before it.
  for (const r of rows.filter((x) => x.role === 'user')) {
    assert.strictEqual(r.text, PROMPT);
    assert.strictEqual(r.text.split(PROMPT).length - 1, 1);
  }
  assert.deepStrictEqual(controller.sent, [PROMPT, PROMPT]);
  assert.ok(rows[3].error && rows[3].retryText === PROMPT, 'the failed row still offers its retry');
  resetChatLog();
});

test('ONE turn at a time: the shared in-flight flag every surface reads', async () => {
  resetChatLog();
  assert.strictEqual(chatTurnInFlight(), false);
  let seenDuringSend = null;
  const controller = {
    attachments: [],
    async send() { seenDuringSend = chatTurnInFlight(); throw new Error('nope'); },
  };
  const run = runLoggedChatTurn(controller, PROMPT, {
    // cleanup runs on the way out and may start the next turn — the flag is already down.
    cleanup: () => { assert.strictEqual(chatTurnInFlight(), false); },
  });
  await run;
  assert.strictEqual(seenDuringSend, true, 'a turn in flight is visible to every surface');
  assert.strictEqual(chatTurnInFlight(), false, 'and cleared once it lands');
  assert.strictEqual(chatLog().length, 2, 'exactly one turn was logged');
  resetChatLog();

  // Both retry/resend entry points consult it, so a click in one surface cannot start a
  // second turn over one the OTHER surface is running (that logged the prompt twice).
  const s = await wireBothSurfaces();
  const user = s.session.appendChatRow({ role: 'user', text: PROMPT });
  s.session.appendChatRow({ role: 'assistant', text: 'failed', error: true, retryText: PROMPT });
  const guarded = async (runner, other) => {
    typeAndSend(runner, 'running');
    const sent = s.ctrl.sent.length;
    other.transcript.querySelector('.chat-retry-cta').fire('click');
    rightClick(other.transcript, rowEl(other.transcript, user.id));
    clickRowItem(openRowMenu(), 'Resend');
    assert.strictEqual(s.ctrl.sent.length, sent, 'neither Retry nor Resend starts a second turn');
    s.ctrl.settle({ reply: 'ok', results: [] });
    await new Promise((r) => setTimeout(r, 0));
  };
  await guarded(s.flyout, s.panel);
  await guarded(s.panel, s.flyout);
  s.panel.transcript.querySelector('.chat-retry-cta').fire('click');
  assert.strictEqual(s.ctrl.sent.at(-1), PROMPT, 'with nothing in flight, Retry sends');
});

// ── §3.0: nothing is rendered after the reply ───────────────────────────────
test('a settled reply carries no progress line and no cancel — the turn is over', async () => {
  stubDom();
  const { renderChatLog } = await import('../../../js/ui/chat/view.js?render-no-aux');
  const transcript = makeEl();
  const log = [
    { id: 1, role: 'user', text: 'outline everything' },
    { id: 2, role: 'assistant', text: '…', pending: true },
  ];
  renderChatLog(transcript, log, {});
  Object.assign(log[1], { pending: false, text: 'Outlined all seventeen.' });
  renderChatLog(transcript, log, {});
  const row = rowsOf(transcript)[1];
  const kids = descendants(row);
  assert.strictEqual(kids.filter((d) => d.classList.contains('chat-typing')).length, 0, 'not answering');
  for (const cls of ['chat-aux', 'chat-aux-spin', 'chat-aux-stop', 'chat-aux-text']) {
    assert.strictEqual(kids.filter((d) => d.classList.contains(cls)).length, 0, `${cls} is gone`);
  }
  assert.strictEqual(row.textContent, 'Outlined all seventeen.', 'the reply, and nothing after it');
  // Even if a stale row carried the old field, there is no renderer for it.
  log[1].aux = { phase: 'refining', done: 4, total: 17 };
  renderChatLog(transcript, log, {});
  assert.strictEqual(descendants(row).filter((d) => d.classList.contains('chat-aux')).length, 0);
  assert.strictEqual(row.textContent, 'Outlined all seventeen.');
  // The view no longer knows the concept at all: no export, and a stale cancel hook is never reached.
  const view = await import('../../../js/ui/chat/view.js');
  assert.deepStrictEqual(Object.keys(view).filter((k) => /aux/i.test(k)), []);
  renderChatLog(transcript, log, { onAuxCancel: () => assert.fail('no aux cancel exists') });
  assert.strictEqual(descendants(row).filter((d) => /aux/.test(d.className)).length, 0);
  const css = COMPONENTS_CSS;
  assert.ok(!/\.chat-aux/.test(css), 'and neither does the stylesheet');
});

// One controller for the whole app (js/llm/chatSession.js): the panel and the menu share
// the memoized controller, its history and the confirm-gated clear.
import { test } from 'node:test';
import assert from 'node:assert';
import {
  sharedChatController, peekChatController, forgetChatController, runChatTurn, chatLog,
  appendChatRow, resetChatLog, clearSharedConversation,
} from '../js/llm/chat/chatSession.js';

// ── One controller for the whole app: the panel and the menu share the history ──
const recordingController = () => ({
  history: [],
  calls: [],
  async send(text, opts) {
    this.calls.push({ text, opts });
    this.history.push({ role: 'user', text });
    this.history.push({ role: 'assistant', text: 'ok' });
    return { reply: `did ${text}`, warnings: [], results: [] };
  },
});

test('sharedChatController memoizes ONE controller per app (panel + menu share it)', () => {
  const app = {};
  let built = 0;
  const create = () => { built++; return recordingController(); };
  assert.strictEqual(peekChatController(app), null, 'nothing is created until first use');
  const fromPanel = sharedChatController(app, { create });
  const fromMenu = sharedChatController(app, { create });
  assert.strictEqual(fromPanel, fromMenu, 'the menu gets the panel\'s controller');
  assert.strictEqual(built, 1, 'built exactly once');
  assert.strictEqual(peekChatController(app), fromPanel);
  // A different app (another editor instance) gets its own.
  const other = sharedChatController({}, { create });
  assert.notStrictEqual(other, fromPanel);
  assert.strictEqual(built, 2);
  forgetChatController(app);
  assert.strictEqual(peekChatController(app), null);
});

test('the injected clearChatConversation capability: confirm-gated shared clear', async () => {
  resetChatLog();
  let allow = false;
  const confirms = [];
  const app = { confirm: async (msg, opts) => { confirms.push({ msg, opts }); return allow; } };
  const cleared = [];
  let captured;
  // Capture the REAL capability closures sharedChatController injects.
  const create = (opts) => { captured = opts; return { clearConversation: () => cleared.push(1) }; };
  sharedChatController(app, { create });
  appendChatRow({ role: 'user', text: 'hi' });

  // Declined: the §10 note; the transcript and the controller stay untouched.
  assert.strictEqual(await captured.clearChatConversation(), 'clear canceled');
  assert.strictEqual(chatLog().length, 1);
  assert.strictEqual(cleared.length, 0);
  assert.match(confirms[0].msg, /Clear this conversation/);
  assert.strictEqual(confirms[0].opts.danger, true);

  // Accepted: replay history AND the visible transcript clear together (emptying
  // the log is what deletes the §12 persisted copy — chatPersistence listens on it).
  allow = true;
  assert.strictEqual(await captured.clearChatConversation(), null);
  assert.strictEqual(cleared.length, 1);
  assert.strictEqual(chatLog().length, 0);
  forgetChatController(app);
  resetChatLog();
});

test('clearSharedConversation clears controller + log, and survives a missing controller', () => {
  resetChatLog();
  appendChatRow({ role: 'user', text: 'orphan row' });
  clearSharedConversation({});   // no controller ever built — still empties the log
  assert.strictEqual(chatLog().length, 0);
  resetChatLog();
});

test('a menu turn goes through the SAME controller, and history stays continuous', async () => {
  const app = {};
  const create = () => recordingController();
  // Panel turn first…
  const panelCtrl = sharedChatController(app, { create });
  await runChatTurn(panelCtrl, 'make it sepia');
  // …then a turn typed into the context menu (it looks the controller up the same way).
  const menuCtrl = sharedChatController(app, { create });
  const res = await runChatTurn(menuCtrl, 'rotate right', { signal: 'sig' });
  assert.strictEqual(menuCtrl, panelCtrl, 'the menu never builds a second controller');
  assert.deepStrictEqual(menuCtrl.calls.map((c) => c.text), ['make it sepia', 'rotate right']);
  assert.strictEqual(menuCtrl.calls[1].opts.signal, 'sig', 'the Stop AbortController signal is forwarded');
  assert.deepStrictEqual(menuCtrl.history.map((m) => m.text),
    ['make it sepia', 'ok', 'rotate right', 'ok'], 'one continuous conversation');
  assert.deepStrictEqual(res, { ok: true, text: 'did rotate right', entry: { reply: 'did rotate right', warnings: [], results: [] } });
  forgetChatController(app);
});

// One turn at a time across every surface: the controller refuses a second send outright, the
// logged-turn frame refuses a turn while another runs, and the panel composer, stencil.prompt and
// the context-menu composer each read the shared flag rather than only their own `sending`.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createChatController, CHAT_BUSY_MESSAGE } from '../../../js/llm/chat/controller.js';
import { makeStencil } from '../../helpers/chatControllerRig.js';
import { wireBothSurfaces, typeAndSend } from '../../helpers/chatSurfacesRig.js';

const tick = () => new Promise((r) => setTimeout(r, 0));

test('the controller rejects a send while one is running, then accepts the next', async () => {
  let answer;
  const client = { chat: () => new Promise((r) => { answer = r; }) };
  const ctrl = createChatController({ stencil: makeStencil().stencil, getClient: () => client });
  const first = ctrl.send('one');
  assert.equal(ctrl.busy, true);
  await assert.rejects(ctrl.send('two'), (e) => e.busy === true && e.message === CHAT_BUSY_MESSAGE);
  await tick();
  answer('Done.');
  await first;
  assert.equal(ctrl.busy, false);
  assert.equal(ctrl.history.filter((m) => m.role === 'user').length, 1, 'the refused send left no trace');
});

test('a turn from the context menu blocks the panel composer and stencil.prompt', async () => {
  const s = await wireBothSurfaces();
  typeAndSend(s.flyout, 'from the menu');
  assert.equal(s.session.chatTurnInFlight(), true);
  typeAndSend(s.panel, 'from the panel');
  await tick();
  await assert.rejects(s.app.chat.prompt('from a script'), (e) => e.busy === true);
  assert.deepEqual(s.ctrl.sent, ['from the menu'], 'one send reached the controller');
  assert.ok(s.notices.some((n) => n.text === CHAT_BUSY_MESSAGE), 'the panel says why it did not send');
  s.ctrl.settle({ reply: 'ok', results: [] });
  await tick();
});

test('a turn from the panel blocks the context-menu composer, and the log gets no stray row', async () => {
  const s = await wireBothSurfaces();
  typeAndSend(s.panel, 'from the panel');
  const rows = s.session.chatLog().length;
  typeAndSend(s.flyout, 'from the menu');
  await tick();
  assert.deepEqual(s.ctrl.sent, ['from the panel']);
  assert.equal(s.session.chatLog().length, rows);
  assert.equal(s.host.sendingFlag, false, 'the menu never marked itself sending');
  s.ctrl.settle({ reply: 'ok', results: [] });
  await tick();
});

test('the logged-turn frame refuses a turn while another runs', async () => {
  const s = await wireBothSurfaces();
  typeAndSend(s.panel, 'first');
  const res = await s.session.runLoggedChatTurn(s.ctrl, 'second');
  assert.equal(res.kind, 'busy');
  assert.deepEqual(s.ctrl.sent, ['first']);
  s.ctrl.settle({ reply: 'ok', results: [] });
  await tick();
});

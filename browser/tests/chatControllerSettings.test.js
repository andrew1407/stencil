// §10 settings ops through a chat turn (js/llm/controller.js): the facade dispatch,
// clearChat deferred to the turn's end, and server resolution for connect/disconnect.
import { test } from 'node:test';
import assert from 'node:assert';
import { createChatController } from '../js/llm/chat/controller.js';
import { makeClient, makeStencil, makeController } from './helpers/chatControllerRig.js';

test('editor-settings ops dispatch through the facade; connect resolves the injected saved servers', async () => {
  const settingsPlan = JSON.stringify({
    version: 1,
    reply: 'done',
    actions: [
      { op: 'theme', mode: 'dark' },
      { op: 'view', lines: false },
      { op: 'connect', server: 'alpha' },
    ],
  });
  const saved = [{ url: 'http://alpha:8090', token: 'stored-tok' }];
  const { controller, calls } = makeController(makeClient(settingsPlan), { savedServers: () => saved });
  const entry = await controller.send('dark mode, hide lines, connect alpha');
  assert.strictEqual(entry.reply, 'done');
  assert.deepStrictEqual(calls, [
    ['darkTheme', true],
    ['apply', { showLines: false }],
    ['connect', saved[0]],          // the SAVED entry — its stored token, never a plan token
  ]);
});

test('clearChat rides a turn end-to-end: deferred past the other actions, notes surface', async () => {
  const clearPlan = JSON.stringify({
    version: 1,
    reply: 'clearing',
    actions: [{ op: 'clearChat' }, { op: 'theme', mode: 'dark' }],
  });
  const seen = [];
  const { controller, calls } = makeController(makeClient(clearPlan), {
    clearChatConversation: async () => { seen.push(calls.map((c) => c[0])); return null; },
  });
  const entry = await controller.send('dark mode, then wipe this chat');
  assert.deepStrictEqual(entry.warnings, []);
  // The confirm/clear happened once, AFTER the theme action that followed it.
  assert.deepStrictEqual(seen, [['darkTheme']]);

  // Declined: the "clear canceled" note comes back as a warning, never a failure.
  const { controller: c2 } = makeController(makeClient(clearPlan), {
    clearChatConversation: async () => 'clear canceled',
  });
  const declined = await c2.send('wipe it');
  assert.ok(declined.warnings.some((w) => w.includes('clearChat: clear canceled')));
});

test('clearChat defers past the §7 auto-continuation — the confirm fires after round two', async () => {
  const loadPlan = JSON.stringify({
    version: 1, reply: 'loading', actions: [{ op: 'blank', color: '#ffffff' }, { op: 'clearChat' }],
  });
  const followUp = JSON.stringify({ version: 1, reply: 'done', actions: [{ op: 'theme', mode: 'dark' }] });
  const client = makeClient([loadPlan, followUp]);
  const seen = [];
  const { controller, calls } = makeController(client, {
    clearChatConversation: async () => { seen.push({ rounds: client.calls.length, ran: calls.map((c) => c[0]) }); return null; },
  });
  const entry = await controller.send('blank page, then wipe this chat');
  assert.strictEqual(client.calls.length, 2, 'the load auto-continued into a second round');
  assert.deepStrictEqual(seen.map((s) => s.rounds), [2], 'one confirm, only after round two went out');
  assert.ok(seen[0].ran.includes('darkTheme'), 'the continuation plan executed before the clear');
  assert.strictEqual(entry.reply, 'done');
});

test('a clearChat asked by BOTH rounds confirms once, at the very end of the turn', async () => {
  const loadPlan = JSON.stringify({
    version: 1, reply: 'loading', actions: [{ op: 'blank', color: '#ffffff' }, { op: 'clearChat' }],
  });
  const followUp = JSON.stringify({ version: 1, reply: 'done', actions: [{ op: 'clearChat' }] });
  const client = makeClient([loadPlan, followUp]);
  let confirms = 0;
  const { controller } = makeController(client, {
    clearChatConversation: async () => { confirms++; return null; },
  });
  const entry = await controller.send('new blank, wipe the chat');
  assert.strictEqual(client.calls.length, 2);
  assert.strictEqual(confirms, 1, 'deduped — the turn shows a single confirm');
  assert.deepStrictEqual(entry.warnings, []);
});

test('connect to an unsaved host rejects with the unknown-server plan error, nothing executed', async () => {
  const planTxt = JSON.stringify({ version: 1, reply: 'x', actions: [{ op: 'connect', server: 'http://evil:1' }] });
  const { controller, calls } = makeController(makeClient(planTxt), {
    savedServers: () => [{ url: 'http://alpha:8090', token: 't' }],
  });
  await assert.rejects(() => controller.send('connect somewhere new'), /Unknown server/);
  assert.deepStrictEqual(calls, []);
});

test('disconnect resolves against the facade\'s live connections', async () => {
  const planTxt = JSON.stringify({ version: 1, reply: 'x', actions: [{ op: 'disconnect', server: 'beta' }] });
  const client = makeClient(planTxt);
  const { stencil, calls } = makeStencil();
  stencil.connections = ['http://alpha:8090', 'https://beta:9090'];
  const controller = createChatController({ stencil, getClient: () => client, savedServers: () => [] });
  await controller.send('drop beta');
  assert.deepStrictEqual(calls, [['disconnect', 'https://beta:9090']]);
});

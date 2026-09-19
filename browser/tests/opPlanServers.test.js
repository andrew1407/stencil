// The §10 settings executor routes and server resolution (js/llm/opPlan.js): saved
// servers for connect, live connections for disconnect, and resolveServer itself.
import { test } from 'node:test';
import assert from 'node:assert';
import { parseOpPlan, executeOpPlan, resolveServer } from '../js/llm/opPlan.js';
import { plan, makeStub } from './helpers/opPlanRig.js';

test('executor: settings ops route through the facade settings paths, in plan order', async () => {
  const { stub, calls } = makeStub();
  const p = parseOpPlan(plan({
    actions: [
      { op: 'theme', mode: 'dark' },
      { op: 'accent', color: '#7c3aed' },
      { op: 'lineStyle', color: '#00ff00', thickness: 3, pointSize: 6, style: 'dashed' },
      { op: 'units', value: 'in' },
      { op: 'view', points: true, lines: false },
      { op: 'rotate', dir: 'left' },
    ],
  }));
  const out = await executeOpPlan(p, stub, {});
  assert.deepStrictEqual(calls, [
    ['darkTheme', true],
    ['mainTheme', '#7c3aed'],
    ['apply', { lineColor: '#00ff00', thickness: 3, pointSize: 6, lineStyle: 'dashed' }],
    ['apply', { unit: 'in' }],
    ['apply', { showPoints: true, showLines: false }],
    ['rotateLeft'],
  ]);
  assert.deepStrictEqual(out, { results: [], warnings: [] });   // settings ops yield no images
});

test('connect resolves ONLY saved servers (exact URL, else unique host); stored token rides', async () => {
  const saved = [
    { url: 'http://alpha:8090', token: 'tA' },
    { url: 'https://beta.example.com:8090', token: 'tB' },
  ];
  const { stub, calls } = makeStub();
  const run = (server) => executeOpPlan(
    parseOpPlan(plan({ actions: [{ op: 'connect', server }] })), stub, { savedServers: () => saved });

  await run('http://alpha:8090');        // exact URL
  await run('beta.example.com');         // unique hostname
  await run('beta.example.com:8090');    // host:port form
  assert.deepStrictEqual(calls, [
    ['connect', saved[0]], ['connect', saved[1]], ['connect', saved[1]],
  ]);
  await assert.rejects(() => run('http://evil:8090'), /Unknown server "http:\/\/evil:8090"/);
  await assert.rejects(() => run('gamma'), /Unknown server "gamma"/);
  assert.strictEqual(calls.length, 3);   // unknown server → nothing executed
  // No saved-servers capability at all → same unknown-server failure.
  const bare = makeStub();
  await assert.rejects(
    () => executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'connect', server: 'alpha' }] })), bare.stub, {}),
    /Unknown server/);
});

test('connect with an ambiguous host fails as unknown server (full URL required)', async () => {
  const saved = [{ url: 'http://srv:8090', token: 'a' }, { url: 'https://srv:9090', token: 'b' }];
  const { stub, calls } = makeStub();
  await assert.rejects(
    () => executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'connect', server: 'srv' }] })), stub, { savedServers: () => saved }),
    /Unknown server "srv".*full URL/);
  assert.deepStrictEqual(calls, []);
});

test('disconnect resolves against LIVE connections the same way', async () => {
  const { stub, calls } = makeStub();
  stub.connections = ['http://alpha:8090', 'https://beta:9090'];
  await executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'disconnect', server: 'beta' }] })), stub, {});
  assert.deepStrictEqual(calls, [['disconnect', 'https://beta:9090']]);
  await assert.rejects(
    () => executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'disconnect', server: 'gamma' }] })), stub, {}),
    /Unknown server "gamma"/);
});

test('resolveServer: exact match beats host match; entries may be strings or objects', () => {
  const entries = [{ url: 'http://a:1', token: 'x' }, 'http://b:2'];
  assert.strictEqual(resolveServer('http://a:1', entries, 'saved servers'), entries[0]);
  assert.strictEqual(resolveServer('b', entries, 'saved servers'), 'http://b:2');
  assert.strictEqual(resolveServer('B:2', entries, 'saved servers'), 'http://b:2');   // host match is case-insensitive
  assert.throws(() => resolveServer('', entries, 'saved servers'), /Unknown server/);
  assert.throws(() => resolveServer('c', [], 'saved servers'), /not among your saved servers/);
});

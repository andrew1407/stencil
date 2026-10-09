// js/core/remote/push.js: a save that meets a peer's newer version (409) merges the server's lines
// as untrusted input — whitelisted like every other ingress — and the union stays inside the caps.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';

installDom().register('notify-balloon', createStubElement('div', { notify() {} }));
const { pushLayout } = await import('../../../js/core/remote/push.js');

// The server answers 409 once, then takes the merged layout; getProject hands back `serverLines`.
const rig = (serverLines, localLines) => {
  const puts = [];
  let conflicts = 1;
  const conn = {
    updateProject: async (id, body) => {
      if (conflicts-- > 0) throw Object.assign(new Error('stale'), { status: 409 });
      puts.push(body.layout);
      return { version: 3 };
    },
    getProject: async () => ({ project: { version: 2 }, layout: { lines: serverLines } }),
  };
  const app = {
    remoteLink: { address: 'http://s', remoteId: 'r1', version: 1 },
    connections: { get: () => conn }, activeProjectId: null, imageBaseName: 'img',
    canvas: { width: 4, height: 3 }, cropRect: { x: 0, y: 0, width: 4, height: 3 }, rotationQuarters: 0,
    lines: localLines, filterDirty: true,
    history: { push() {} }, renderer: { redraw() {} },
  };
  const hooks = { saved() {}, toast() {}, reload() {}, adoptServerFilter() {}, live: () => true };
  return { app, hooks, puts };
};

test('a 409 merge rebuilds the peer lines through the whitelist before they join ours', async () => {
  const peer = [
    { points: [{ x: 1, y: 2 }, { x: 'NaN', y: 3 }, null], color: '#f00', onclick: 'x', locked: 1 },
    'not a line',
  ];
  const mine = { points: [{ x: 9, y: 9 }], color: '#00f' };
  const { app, hooks, puts } = rig(peer, [mine]);
  assert.ok(await pushLayout(app, hooks));
  assert.deepEqual(app.lines, [{ points: [{ x: 1, y: 2 }], color: '#f00', locked: true }, mine]);
  assert.equal(puts.length, 1, 'the merged layout is what the retry saves');
  assert.equal(puts[0].lines, app.lines);
});

test('a 409 merge cuts the union at the total-points cap, peer lines first', async () => {
  const full = Array.from({ length: 100_000 }, (_, i) => ({ x: i, y: 0 }));
  const peer = Array.from({ length: 10 }, (_, i) => ({ points: full, color: `#00000${i}` }));
  const { app, hooks } = rig(peer, [{ points: [{ x: 1, y: 1 }] }]);
  await pushLayout(app, hooks);
  assert.equal(app.lines.reduce((n, l) => n + l.points.length, 0), 1_000_000);
  assert.equal(app.lines.length, 10, 'our line comes after the budget is spent');
});

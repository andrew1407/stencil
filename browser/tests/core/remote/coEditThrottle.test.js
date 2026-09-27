// BR-11: a co-edit push sends the LAYOUT at the debounce and the rendered result only when the
// editing goes quiet; a peer's edit on the same original (equal originalHash) is adopted in place
// with the undo history kept; a steady session toasts on a change of state only.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';
import { recordingCtx } from '../../helpers/recordingCtx.js';

const toasts = [];
// The result renders inline here (no Worker): one stub canvas that encodes to a fixed PNG.
const resultCanvas = () => ({ width: 0, height: 0, getContext: () => recordingCtx().ctx,
  toBlob: (cb) => cb(new Blob([new Uint8Array([7])])) });
const doc = installDom({ createElement: (tag) => (tag === 'canvas' ? resultCanvas() : createStubElement(tag)) });
doc.register('notify-balloon', createStubElement('div', { notify: (msg) => toasts.push(msg) }));
const { RemoteSyncController } = await import('../../../js/core/remote/syncController.js');

// The decode boundary: a reload is the real loadImageFromFile reading the fetched original, so
// each read counts as one load of the rig in play; the decode itself never needs to finish.
let current = null;
globalThis.FileReader = class { readAsDataURL() { current.calls.loads++; } };

const HASH = 'a'.repeat(64);
const RECORD = { id: 'r1', hasImage: true, originalPath: 'projects/r1/original.png', imageW: 4, imageH: 3, originalHash: HASH };
const settle = () => new Promise((r) => setImmediate(r));

const rig = () => {
  const calls = { put: [], files: [], fetches: 0, loads: 0, resets: 0, pushes: [], settles: [] };
  let version = 1;
  const server = { layout: { lines: [], cropRect: { x: 0, y: 0, w: 4, h: 3 }, rotationQuarters: 0 }, record: { ...RECORD } };
  const conn = {
    url: 'http://s',
    updateProject: async (id, body) => { calls.put.push(body.layout); return { version: ++version }; },
    putFile: async (id, kind) => { calls.files.push(kind); version++; },
    getProject: async () => ({ project: { ...server.record, version }, layout: server.layout }),
    fetchFile: async () => { calls.fetches++; return new Blob([new Uint8Array([1])], { type: 'image/png' }); },
  };
  const app = {
    remoteLink: { address: 'http://s', remoteId: 'r1', version: 1 },
    connections: { get: () => conn }, activeProjectId: null, imageBaseName: 'img',
    image: {}, originalImage: { width: 4, height: 3 }, canvas: { width: 4, height: 3 },
    cropRect: { x: 0, y: 0, width: 4, height: 3 }, rotationQuarters: 0,
    lines: [], selectedLineIdx: -1, selectedLines: [], coordLineIdx: -1,
    // The view an undo step names, taken from the original as ImageModel.restoreView takes it.
    imageModel: {
      roundRect: (r) => ({ x: r.x, y: r.y, width: r.w ?? r.width, height: r.h ?? r.height }),
      restoreView: (m) => {
        if (JSON.stringify(m.cropRect) === JSON.stringify(app.cropRect) && m.rotationQuarters === app.rotationQuarters) return false;
        Object.assign(app, { cropRect: m.cropRect, rotationQuarters: m.rotationQuarters });
        return true;
      },
      settleView: (opts) => calls.settles.push(opts),
    },
    history: { push: (m) => calls.pushes.push(m.lines.length), reset: () => { calls.resets++; } },
    renderer: { redraw() {}, restingBase: () => ({}) }, coordTable: { update() {} }, updateButtons() {}, deselectLine() {},
    storage: { saveSoon() {}, promoteTemporaryToProject() {}, store: { getMeta: () => null } },
    tabs: { reportActive() {} },
    settings: { syncFormulaUI() {}, showFormulaError() {} },
  };
  current = { app, conn, calls, server, sync: new RemoteSyncController(app), bump: () => ++version };
  return current;
};

test('a burst of edits pushes the layout at the debounce and the result once, when idle', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout', 'Date'] });
  toasts.length = 0;
  const { app, sync, calls } = rig();
  for (let i = 0; i < 40; i++) {
    app.lines = [{ points: [{ x: i, y: 0 }] }];
    sync.scheduleRemoteSync();
    t.mock.timers.tick(100);
    await settle();
  }
  t.mock.timers.tick(400);
  await settle();
  assert.ok(calls.put.length >= 2, `the layout keeps flowing (${calls.put.length} pushes)`);
  assert.deepEqual(calls.files, [], 'no result upload while the edits keep coming');
  t.mock.timers.tick(10_000);
  await settle(); await settle();
  assert.deepEqual(calls.files, ['result'], 'one result, after the burst');
  assert.deepEqual(toasts, ['Saved to server'], 'one toast for the whole session');
});

test('an explicit save sends the layout and the result now, and always toasts', async () => {
  toasts.length = 0;
  const { sync, calls } = rig();
  await sync.saveToServer();
  await sync.saveToServer();
  assert.equal(calls.put.length, 2);
  assert.deepEqual(calls.files, ['result', 'result']);
  assert.deepEqual(toasts, ['Saved to server', 'Saved to server']);
});

test('a peer edit on the same picture: layout in place, history kept, original not fetched', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { app, sync, calls, server, bump } = rig();
  sync.noteServerImage(RECORD);
  server.layout = { ...server.layout, lines: [{ points: [{ x: 1, y: 1 }, { x: 2, y: 2 }], color: '#ff0000' }] };
  const v = bump();
  sync.onServerProjectEvent({ type: 'project-event', event: 'updated', project: { ...RECORD, version: v } });
  await settle();
  assert.equal(app.lines.length, 1, 'the peer\'s line is on the canvas');
  assert.deepEqual(calls.pushes, [1], 'one undo step for the peer\'s edit');
  assert.equal(calls.resets, 0, 'the undo history is kept');
  assert.equal(calls.fetches, 0, 'the original is not downloaded again');
  assert.equal(calls.loads, 0);
  assert.equal(app.remoteLink.version, v, 'the link adopts the server version');
  t.mock.timers.tick(200);
  // The same layout again (a peer's result upload bumped the version): no empty undo step.
  sync.onServerProjectEvent({ type: 'project-event', event: 'updated', project: { ...RECORD, version: bump() } });
  await settle();
  assert.deepEqual(calls.pushes, [1]);
});

test('a peer\'s crop and turn over the same original: in place, one undo step, nothing fetched or pushed', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { app, sync, calls, server, bump } = rig();
  sync.noteServerImage(RECORD);
  server.layout = { ...server.layout, cropRect: { x: 0, y: 1, w: 3, h: 2 }, rotationQuarters: 1 };
  sync.onServerProjectEvent({ type: 'project-event', event: 'updated', project: { ...RECORD, version: bump() } });
  await settle();
  assert.deepEqual([app.cropRect, app.rotationQuarters], [{ x: 0, y: 1, width: 3, height: 2 }, 1]);
  assert.deepEqual([calls.pushes, calls.resets, calls.fetches, calls.loads], [[0], 0, 0, 0]);
  assert.deepEqual(calls.settles, [{ sync: false }], 'the view settles without a push back');
  t.mock.timers.tick(200);
  sync.onServerProjectEvent({ type: 'project-event', event: 'updated', project: { ...RECORD, version: bump() } });
  await settle();
  assert.deepEqual(calls.pushes, [0], 'the same layout again is no step');
});

test('a new picture on the server reloads it', async () => {
  const { sync, calls, server, bump } = rig();
  sync.noteServerImage(RECORD);
  server.record = { ...RECORD, imageW: 8, imageH: 6, originalHash: 'c'.repeat(64) };
  sync.onServerProjectEvent({ type: 'project-event', event: 'updated', project: { ...server.record, version: bump() } });
  await settle(); await settle();
  assert.equal(calls.fetches, 1);
  assert.equal(calls.loads, 1);
});

// A peer that REPLACES the original with a same-size image of the same type: only the hash tells.
test('a replaced original of the same size and type reloads, by its hash', async () => {
  const { sync, calls, server, bump } = rig();
  sync.noteServerImage(RECORD);
  server.record = { ...RECORD, originalHash: 'b'.repeat(64) };
  sync.onServerProjectEvent({ type: 'project-event', event: 'updated', project: { ...server.record, version: bump() } });
  await settle(); await settle();
  assert.equal(calls.fetches, 1, 'the new original is downloaded');
  assert.equal(calls.loads, 1);
  assert.deepEqual(calls.pushes, [], 'no layout adopted over the old picture');
});

// No hash on either side is "unknown": the full reload, as before the in-place path existed.
for (const [name, noted, fresh] of [
  ['the server record has none', RECORD, { ...RECORD, originalHash: undefined }],
  ['none was noted (an older server, or a project this editor created)', null, RECORD],
  ['neither has one', { ...RECORD, originalHash: '' }, { ...RECORD, originalHash: '' }],
]) {
  test(`a missing originalHash reloads: ${name}`, async () => {
    const { app, sync, calls, server, bump } = rig();
    app.originalImage = { width: 4, height: 3 };
    if (noted) sync.noteServerImage(noted);
    server.record = fresh;
    server.layout = { ...server.layout, lines: [{ points: [{ x: 1, y: 1 }] }] };
    sync.onServerProjectEvent({ type: 'project-event', event: 'updated', project: { ...fresh, version: bump() } });
    await settle(); await settle();
    assert.equal(calls.fetches, 1);
    assert.equal(calls.loads, 1);
    assert.deepEqual(calls.pushes, []);
  });
}

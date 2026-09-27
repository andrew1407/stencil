// The original this editor uploads is the picture it notes: a replaced original is named by the
// server's answer (or the bytes' own SHA-256), and a create never takes a re-read that could name a
// peer's replacement — so a peer's next edit keeps the history only on the picture really on screen.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { installDom, createStubElement } from '../../helpers/dom.js';
import { recordingCtx } from '../../helpers/recordingCtx.js';

// The result renders inline here (no Worker): one stub canvas that encodes to a fixed PNG.
const resultCanvas = () => ({ width: 0, height: 0, getContext: () => recordingCtx().ctx,
  toBlob: (cb) => cb(new Blob([new Uint8Array([7])])) });
const doc = installDom({ createElement: (tag) => (tag === 'canvas' ? resultCanvas() : createStubElement(tag)) });
doc.register('notify-balloon', createStubElement('div', { notify() {} }));
const { settleLoadedImage } = await import('../../../js/core/image/settle.js');
const { RemoteSyncController } = await import('../../../js/core/remote/syncController.js');
const { originalHashOf, withOriginal, imageSignature } = await import('../../../js/core/remote/peerLayout.js');

// The decode boundary: a reload is the real loadImageFromFile reading the fetched original, so each
// read counts as one load of the rig in play; the decode itself never needs to finish.
let current = null;
globalThis.FileReader = class { readAsDataURL() { current.calls.loads++; } };

const BYTES = new Uint8Array([0x89, 0x50, 0x4e, 0x47, 1, 2, 3]);
const OURS = createHash('sha256').update(BYTES).digest('hex');
const OLD = 'a'.repeat(64);
const PEER = 'f'.repeat(64);
const settle = () => new Promise((r) => setImmediate(r));
const answer = (body) => ({ json: async () => body });

const rig = ({ link = null, answers = true, onOriginal = () => {} } = {}) => {
  const calls = { loads: 0, resets: 0, pushes: [], fetches: 0 };
  const server = { record: { id: 'r1', hasImage: true, originalHash: OLD, version: 1 },
    layout: { lines: [], cropRect: { x: 0, y: 0, w: 4, h: 3 }, rotationQuarters: 0 } };
  const conn = {
    url: 'http://s',
    createProject: async () => ({ id: 'r1', version: 0 }),
    putFile: async (id, kind, bytes) => {
      server.record = { ...server.record, version: server.record.version + 1 };
      if (kind !== 'original') return answer({ path: 'p', w: 4, h: 3 });
      server.record.originalHash = createHash('sha256').update(bytes).digest('hex');
      onOriginal(server);
      return answer(answers ? { path: 'p', w: 4, h: 3, originalHash: server.record.originalHash } : { path: 'p', w: 4, h: 3 });
    },
    updateProject: async () => ({ version: ++server.record.version }),
    getProject: async () => ({ project: { ...server.record }, layout: server.layout }),
    fetchFile: async () => { calls.fetches++; return new Blob([BYTES], { type: 'image/png' }); },
  };
  const app = {
    remoteLink: link, connections: { get: () => conn }, activeProjectId: null,
    imageBaseName: 'img', imageExt: 'png', originalImage: { width: 4, height: 3 },
    image: {}, canvas: { width: 4, height: 3 }, rotationQuarters: 0, lines: [],
    selectedLineIdx: -1, selectedLines: [], coordLineIdx: -1, pendingLines: null,
    imageModel: {
      roundRect: (r) => ({ x: r.x, y: r.y, width: r.width ?? r.w, height: r.height ?? r.h }),
      defaultCropRect: () => ({ x: 0, y: 0, width: 4, height: 3 }), rebuildCroppedImage() {}, restoreView: () => false,
    },
    history: { push: (m) => calls.pushes.push(m.lines.length), reset: () => { calls.resets++; } },
    renderer: { redraw() {}, restingBase: () => ({}) }, coordTable: { update() {} }, updateButtons() {}, deselectLine() {},
    updateInfo() {}, updateCoordStatus() {},
    storage: { save() {}, saveSoon() {}, promoteTemporaryToProject() {}, store: { getMeta: () => null } },
    tabs: { reportActive() {}, reportIncognito() {} },
    settings: { syncFormulaUI() {}, showFormulaError() {} },
  };
  app.remoteSync = new RemoteSyncController(app);
  current = { app, calls, server };
  return current;
};

const load = (app, plan) => settleLoadedImage(app, new File([BYTES], 'img.png', { type: 'image/png' }),
  { landing: false, keepZoom: true }, { remoteLayout: null, ...plan });

// A peer's layout edit lands on whatever picture the server now holds.
const peerEdits = async ({ app, server }) => {
  server.layout = { ...server.layout, lines: [{ points: [{ x: 1, y: 1 }, { x: 2, y: 2 }] }] };
  server.record = { ...server.record, version: server.record.version + 1 };
  app.remoteSync.onServerProjectEvent({ type: 'project-event', event: 'updated', project: { ...server.record } });
  await settle(); await settle();
};

for (const [name, answers] of [['the server answers its hash', true], ['an older server answers none', false]]) {
  test(`a replaced original is noted, so a peer's next edit keeps the history: ${name}`, async (t) => {
    t.mock.timers.enable({ apis: ['Date'] });
    const r = rig({ link: { address: 'http://s', remoteId: 'r1', version: 1 }, answers });
    r.app.remoteSync.noteServerImage(r.server.record); // opened on the old picture
    await load(r.app, { replaceInPlace: true, keptLines: [] });
    assert.equal(r.server.record.originalHash, OURS);
    const resets = r.calls.resets;
    t.mock.timers.tick(1000); // past the echo window of the save the replace made
    await peerEdits(r);
    assert.equal(r.calls.loads, 0, 'no full reload of the picture this editor just uploaded');
    assert.equal(r.calls.fetches, 0);
    assert.equal(r.calls.resets, resets, 'the undo history is kept');
    assert.deepEqual(r.calls.pushes, [1], 'the peer\'s edit is one undo step');
  });
}

test('a created project notes the bytes it uploaded, so a peer edit on them is adopted in place', async () => {
  const r = rig();
  await load(r.app, { remoteCreateAddress: 'http://s' });
  assert.equal(r.app.remoteLink.remoteId, 'r1');
  await peerEdits(r);
  assert.equal(r.calls.loads, 0);
  assert.deepEqual(r.calls.pushes, [1]);
});

test('a peer replacing the original right after the create is not mistaken for this picture', async () => {
  const r = rig({ onOriginal: (server) => { server.record.originalHash = PEER; } });
  await load(r.app, { remoteCreateAddress: 'http://s' });
  await peerEdits(r);
  assert.equal(r.calls.loads, 1, 'the peer\'s picture is loaded, not a layout painted over ours');
  assert.deepEqual(r.calls.pushes, []);
});

test('originalHashOf is the server\'s SHA-256; withOriginal swaps it and keeps the blank fill', async () => {
  assert.equal(await originalHashOf(BYTES), OURS);
  const sig = imageSignature({ hasImage: true, originalHash: OLD, blankColor: '#ffffff' });
  assert.equal(withOriginal(sig, OURS), imageSignature({ hasImage: true, originalHash: OURS, blankColor: '#ffffff' }));
  assert.equal(withOriginal('', OURS), imageSignature({ hasImage: true, originalHash: OURS }));
  assert.equal(withOriginal(sig, ''), '', 'an unknown hash names no picture');
});

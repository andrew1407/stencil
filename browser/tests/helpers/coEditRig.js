// The co-edit rig behind tests/core/remote/coEdit*.test.js: a RemoteSyncController over a versioned
// fake server (a stale PUT 409s) and a stub app, with the toasts and the loads it caused counted.
import { installDom, createStubElement } from './dom.js';
import { recordingCtx } from './recordingCtx.js';

export const toasts = [];
// The result renders inline here (no Worker): one stub canvas that encodes to a fixed PNG.
const resultCanvas = () => ({ width: 0, height: 0, getContext: () => recordingCtx().ctx,
  toBlob: (cb) => cb(new Blob([new Uint8Array([7])])) });
const doc = installDom({ createElement: (tag) => (tag === 'canvas' ? resultCanvas() : createStubElement(tag)) });
doc.register('notify-balloon', createStubElement('div', { notify: (msg) => toasts.push(msg) }));
const { RemoteSyncController } = await import('../../js/core/remote/syncController.js');

// The decode boundary: a reload is the real loadImageFromFile reading the fetched original, so
// each read counts as one load of the rig in play; the decode itself never needs to finish.
let current = null;
globalThis.FileReader = class { readAsDataURL() { current.calls.loads++; } };

export const HASH = 'a'.repeat(64);
export const RECORD = { id: 'r1', hasImage: true, originalPath: 'projects/r1/original.png', imageW: 4, imageH: 3, originalHash: HASH };
export const settle = () => new Promise((r) => setImmediate(r));

export const rig = () => {
  const calls = { put: [], files: [], fetches: 0, loads: 0, resets: 0, pushes: [], settles: [], gets: 0, conflicts: 0 };
  let version = 1;
  const server = { layout: { lines: [], cropRect: { x: 0, y: 0, w: 4, h: 3 }, rotationQuarters: 0 }, record: { ...RECORD }, gate: null };
  // Versioned as the real server is: a stale PUT 409s, and a write stores the layout it carried.
  const conn = {
    url: 'http://s',
    updateProject: async (id, body) => {
      if (body.version !== version) { calls.conflicts++; throw Object.assign(new Error('stale'), { status: 409 }); }
      calls.put.push(body.layout);
      server.layout = { ...server.layout, lines: body.layout.lines };
      return { version: ++version };
    },
    putFile: async (id, kind) => { calls.files.push(kind); version++; },
    getProject: async () => { calls.gets++; await server.gate; return { project: { ...server.record, version }, layout: server.layout }; },
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

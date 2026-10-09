// js/core/remote/push.js over a fake server: a peer's save landing between our result upload and
// its version re-read is never adopted as ours, so our next push 409s and merges the peer's lines;
// a reply that lands after the editor switched projects is dropped, never written into the new one.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';
import { recordingCtx } from '../../helpers/recordingCtx.js';

const resultCanvas = () => ({ width: 0, height: 0, getContext: () => recordingCtx().ctx,
  toBlob: (cb) => cb(new Blob([new Uint8Array([7])])) });
installDom({ createElement: (tag) => (tag === 'canvas' ? resultCanvas() : createStubElement(tag)) })
  .register('notify-balloon', createStubElement('div', { notify() {} }));
const { pushLayout, putResult, captureResult } = await import('../../../js/core/remote/push.js');

const line = (x, color = '#f00') => ({ points: [{ x, y: 0 }, { x, y: 1 }], color });

// A versioned record: every write must name the current version, and a file write bumps it by one.
const fakeServer = () => {
  const srv = { version: 1, lines: [], puts: 0, beforeReRead: null, gate: null };
  srv.conn = {
    url: 'http://s',
    updateProject: async (id, body) => {
      await srv.gate;
      if (body.version !== srv.version) throw Object.assign(new Error('stale'), { status: 409 });
      srv.puts++;
      srv.lines = body.layout.lines;
      return { version: ++srv.version };
    },
    putFile: async () => { srv.version++; srv.beforeReRead?.(); srv.beforeReRead = null; },
    getProject: async () => { await srv.gate; return { project: { version: srv.version }, layout: { lines: srv.lines } }; },
  };
  srv.peerSaves = (lines) => { srv.lines = lines; srv.version++; };
  return srv;
};

const appOn = (srv, lines) => {
  const mementos = [];
  const app = {
    remoteLink: { address: 'http://s', remoteId: 'r1', version: srv.version },
    connections: { get: () => srv.conn }, activeProjectId: null, imageBaseName: 'img', image: {},
    canvas: { width: 4, height: 3 }, cropRect: { x: 0, y: 0, width: 4, height: 3 }, rotationQuarters: 0,
    lines, filterDirty: true, history: { push: (m) => mementos.push(m) },
    renderer: { redraw() {}, restingBase: () => ({}) },
  };
  return { app, mementos };
};
const hooks = { saved() {}, toast() {}, reload() {}, adoptServerFilter() {}, live: () => true };

// The editor moves to project B while a round trip for A is out.
const B = { address: 'http://s', remoteId: 'r2', version: 7 };
const gated = (srv) => { let open; srv.gate = new Promise((r) => { open = r; }); return open; };
const switchTo = (app, lines) => { app.remoteLink = { ...B }; app.lines = lines; };

test('a result upload adopts the one version its own write produced', async () => {
  const srv = fakeServer();
  const { app } = appOn(srv, [line(1)]);
  await putResult(app, captureResult(app), hooks);
  assert.equal(app.remoteLink.version, 2);
  assert.ok(await pushLayout(app, hooks), 'the next push is not a spurious 409');
  assert.equal(srv.puts, 1);
});

test('a peer save between the upload and its re-read is merged, never overwritten', async () => {
  const srv = fakeServer();
  const mine = line(1);
  const theirs = line(5, '#00f');
  const { app } = appOn(srv, [mine]);
  srv.beforeReRead = () => srv.peerSaves([theirs]);
  await putResult(app, captureResult(app), hooks);
  assert.equal(app.remoteLink.version, 1, 'a version whose layout this tab never read is not adopted');
  assert.ok(await pushLayout(app, hooks));
  assert.deepEqual(srv.lines.map((l) => l.points[0].x), [5, 1], 'the peer line survives our push');
});

test('repeated 409 passes merge against the last peer set and push one memento', async () => {
  const srv = fakeServer();
  const mine = line(1);
  const { app, mementos } = appOn(srv, [mine]);
  srv.peerSaves([line(5, '#00f')]);
  let conflicts = 0;
  const update = srv.conn.updateProject;
  // The peer moves its line once more while our first retry is in flight.
  srv.conn.updateProject = async (id, body) => {
    if (conflicts++ === 1) srv.peerSaves([line(6, '#00f')]);
    return update(id, body);
  };
  assert.ok(await pushLayout(app, hooks));
  assert.deepEqual(app.lines.map((l) => l.points[0].x), [6, 1], 'the moved line is not resurrected');
  assert.deepEqual(srv.lines.map((l) => l.points[0].x), [6, 1]);
  assert.equal(mementos.length, 1, 'one undo step for the save');
});

test('a switch mid-PUT: B keeps its own link and lines, A\'s reply is dropped', async () => {
  const srv = fakeServer();
  const { app, mementos } = appOn(srv, [line(1)]);
  const open = gated(srv);
  const push = pushLayout(app, hooks);
  const bLines = [line(9, '#0f0')];
  switchTo(app, bLines);
  open();
  assert.equal(await push, null);
  assert.deepEqual(app.remoteLink, B, 'A\'s new version is not written into B\'s link');
  assert.equal(app.lines, bLines);
  assert.equal(srv.puts, 1, 'the write itself reached A on the server');
  assert.equal(mementos.length, 0);
});

test('a switch while the 409 re-read is out: A\'s peer lines are never merged into B', async () => {
  const srv = fakeServer();
  const { app, mementos } = appOn(srv, [line(1)]);
  srv.peerSaves([line(5, '#00f')]);
  let open = null;
  const read = srv.conn.getProject;
  srv.conn.getProject = async (id) => { if (!open) open = gated(srv); return read(id); };
  const push = pushLayout(app, hooks);
  await new Promise((r) => setImmediate(r));
  const bLines = [line(9, '#0f0')];
  switchTo(app, bLines);
  open();
  assert.equal(await push, null);
  assert.equal(app.lines, bLines);
  assert.deepEqual(app.remoteLink, B);
  assert.equal(mementos.length, 0, 'no undo step on a project that merged nothing');
});

test('the controller detached and reattached to the same project: the stale reply is dropped', async () => {
  const srv = fakeServer();
  const { app } = appOn(srv, [line(1)]);
  let epoch = 0;
  const open = gated(srv);
  const push = pushLayout(app, { ...hooks, live: () => epoch === 0 });
  epoch++;
  open();
  assert.equal(await push, null);
  assert.equal(app.remoteLink.version, 1);
});

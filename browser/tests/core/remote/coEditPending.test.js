// A peer's change behind a pending co-edit push waits for the push, whose 409 merge keeps the
// unpushed local line; detach() on a project switch drops the pending push and any reply in flight.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { toasts, RECORD, settle, rig } from '../../helpers/coEditRig.js';

const line = (x, color) => ({ points: [{ x, y: 0 }, { x, y: 1 }], color });
const xs = (lines) => lines.map((l) => l.points[0].x);
const peerEvent = (sync, project) => sync.onServerProjectEvent({ type: 'project-event', event: 'updated', project });

test('a peer\'s change behind a pending push waits for it: the local line survives into the payload', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout', 'Date'], now: 10_000 });
  const { app, sync, calls, server, bump } = rig();
  sync.noteServerImage(RECORD);
  app.lines = [line(1, '#ff0000')];
  sync.scheduleRemoteSync();
  server.layout = { ...server.layout, lines: [line(5, '#0000ff')] };
  peerEvent(sync, { ...RECORD, version: bump() });
  await settle();
  assert.deepEqual(xs(app.lines), [1], 'no pull while the push is pending');
  t.mock.timers.tick(400);
  for (let i = 0; i < 6; i++) await settle();
  assert.equal(calls.conflicts, 1, 'the push 409s against the peer\'s version');
  assert.deepEqual(xs(calls.put.at(-1).lines).sort(), [1, 5], 'the merged payload carries both lines');
  assert.deepEqual(xs(app.lines).sort(), [1, 5]);
  assert.deepEqual([calls.resets, calls.fetches, calls.loads], [0, 0, 0], 'no reload, history kept');
  assert.equal(calls.gets, 1, 'the merge\'s read only; the deferred pull is skipped as merged');
  assert.equal(app.remoteLink.version, 3);
  t.mock.timers.tick(200);
  peerEvent(sync, { ...RECORD, version: bump() });
  await settle();
  assert.equal(calls.gets, 2, 'with nothing pending the same event pulls at once');
});

test('a new picture behind a pending push is reloaded once the push is through', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout', 'Date'], now: 10_000 });
  const { app, sync, calls, server, bump } = rig();
  sync.noteServerImage(RECORD);
  app.lines = [line(1, '#ff0000')];
  sync.scheduleRemoteSync();
  server.record = { ...RECORD, originalHash: 'c'.repeat(64) };
  peerEvent(sync, { ...server.record, version: bump() });
  await settle();
  assert.equal(calls.fetches, 0, 'the picture waits behind the push');
  t.mock.timers.tick(400);
  for (let i = 0; i < 8; i++) await settle();
  assert.ok(calls.put.length >= 1, 'the local edit was pushed first');
  assert.deepEqual([calls.fetches, calls.loads], [1, 1], 'then the new original is loaded');
});

test('detach: the pending push is dropped and a reload in flight lands nowhere', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout', 'Date'], now: 10_000 });
  toasts.length = 0;
  const { app, sync, calls, server, bump } = rig();
  sync.noteServerImage(RECORD);
  app.lines = [line(1, '#ff0000')];
  sync.scheduleRemoteSync();
  sync.detach();
  t.mock.timers.tick(2000);
  await settle();
  assert.deepEqual(calls.put, [], 'the switch saved the edit locally; nothing goes to the old project');
  // A reload that was out when the editor switched: the peer's lines never reach the next project.
  sync.noteServerImage(RECORD);
  app.remoteLink = { address: 'http://s', remoteId: 'r1', version: 1 };
  let open;
  server.gate = new Promise((r) => { open = r; });
  server.layout = { ...server.layout, lines: [line(5, '#0000ff')] };
  peerEvent(sync, { ...RECORD, version: bump() });
  await settle();
  assert.equal(calls.gets, 1);
  sync.detach();
  app.remoteLink = { address: 'http://s', remoteId: 'r2', version: 9 };
  open();
  for (let i = 0; i < 4; i++) await settle();
  assert.deepEqual(xs(app.lines), [1]);
  assert.equal(app.remoteLink.version, 9);
  assert.deepEqual(toasts, []);
});

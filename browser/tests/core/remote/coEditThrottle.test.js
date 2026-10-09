// BR-11: a co-edit push sends the LAYOUT at the debounce and the rendered result only when the
// editing goes quiet; a peer's edit on the same original (equal originalHash) is adopted in place
// with the undo history kept; a steady session toasts on a change of state only.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { toasts, RECORD, settle, rig } from '../../helpers/coEditRig.js';

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

// A peer's save right after ours lands inside our 150 ms echo window: asked again after it.
test('a newer peer event inside the echo window is pulled once the window passes', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout', 'Date'], now: 10_000 });
  const { app, sync, calls, server, bump } = rig();
  sync.noteServerImage(RECORD);
  await sync.saveToServer();
  const gets = calls.gets;
  server.layout = { ...server.layout, lines: [{ points: [{ x: 1, y: 1 }, { x: 2, y: 2 }], color: '#0000ff' }] };
  sync.onServerProjectEvent({ type: 'project-event', event: 'updated', project: { ...RECORD, version: bump() } });
  await settle();
  assert.equal(calls.gets, gets, 'not pulled inside the window');
  t.mock.timers.tick(150);
  await settle(); await settle();
  assert.equal(calls.gets, gets + 1);
  assert.equal(app.lines.length, 1, 'the peer\'s line is adopted');
});

test('our own echo that beat our PUT\'s reply is still dropped after the window', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout', 'Date'], now: 10_000 });
  const { app, sync, calls, conn } = rig();
  sync.noteServerImage(RECORD);
  const update = conn.updateProject;
  conn.updateProject = async (id, body) => {
    const out = await update(id, body);
    sync.onServerProjectEvent({ type: 'project-event', event: 'updated', project: { ...RECORD, version: out.version } });
    return out;
  };
  await sync.saveToServer();
  const gets = calls.gets;
  t.mock.timers.tick(150);
  await settle(); await settle();
  assert.equal(calls.gets, gets, 'the link reached that version: nothing to pull');
  assert.equal(app.remoteLink.version, 3, 'the layout write, then the result write');
});

// A peer saved while our events feed was down: the reopened feed re-reads the record.
test('a resumed feed pulls a version saved while it was down, and nothing when none was', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout', 'Date'], now: 10_000 });
  const { app, sync, calls, server, conn, bump } = rig();
  sync.noteServerImage(RECORD);
  sync.onServerProjectEvent({ type: 'feed-resumed' }, conn);
  await settle(); await settle();
  assert.equal(calls.gets, 1, 'one read to learn the version');
  assert.equal(app.lines.length, 0, 'the same version: nothing adopted');
  server.layout = { ...server.layout, lines: [{ points: [{ x: 1, y: 1 }, { x: 2, y: 2 }], color: '#0000ff' }] };
  bump();
  sync.onServerProjectEvent({ type: 'feed-resumed' }, conn);
  await settle(); await settle(); await settle();
  assert.equal(app.lines.length, 1, 'the missed peer line is adopted');
});

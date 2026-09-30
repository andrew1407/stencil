// copyProject (core/project/copy/copyProject.js) end to end over a real ProjectsStore: the name,
// the saved row, the chat hand-over, the server path and every way a copy opens.
import { test, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import { IMG, THUMB, META, makeCopyRig, makeServer } from '../../../helpers/projectCopyRig.js';

const savedFile = globalThis.File;
beforeEach(() => { globalThis.File = savedFile ?? class { constructor(parts, name) { this.name = name; } }; });
afterEach(() => { globalThis.File = savedFile; });

test('a stored row copies to "<name>-copy", then "-copy(1)"', async () => {
  const { ctrl, store } = makeCopyRig();
  const first = await ctrl.copyProject({ id: 'p1', what: 'layout' });
  const second = await ctrl.copyProject({ id: 'p1', what: 'layout' });
  assert.equal(store.getMeta(first).name, 'Cat-copy');
  assert.equal(store.getMeta(second).name, 'Cat-copy(1)');
  assert.equal(store.get(first).payload.layout.lines.length, 1);
  assert.equal(store.getMeta(first).thumbnail, THUMB, 'the source thumbnail travels as a data URL');
});

test('image-only leaves the thumbnail for the first open and the chat behind', async () => {
  const { ctrl, store, calls } = makeCopyRig();
  const id = await ctrl.copyProject({ id: 'p1', what: 'image' });
  assert.equal(store.getMeta(id).thumbnail ?? null, null);
  assert.deepEqual(store.get(id).payload.layout.lines, []);
  assert.ok(!calls.some(([k]) => k === 'chat'));
});

test('a whole-project copy takes the chat and the meta; the list hears of it', async () => {
  const { ctrl, store, calls } = makeCopyRig();
  const id = await ctrl.copyProject({ id: 'p1', what: 'project' });
  assert.deepEqual(calls.find(([k]) => k === 'chat').slice(1), [{ id: 'p1' }, { id }]);
  assert.equal(store.getMeta(id).color, '#112233');
  assert.ok(calls.some(([k, d]) => k === 'changed' && d.id === id));
});

test('the live editor (id null) is read through the host, unsaved edits and all', async () => {
  const live = { srcId: null, meta: { name: 'Unsaved', hasImage: true }, payload: { image: IMG, layout: { lines: [] } } };
  const { ctrl, store } = makeCopyRig({ live });
  const id = await ctrl.copyProject({ what: 'layout' });
  assert.equal(store.getMeta(id).name, 'Unsaved-copy');
});

test('opening: here switches, a new tab gets the pre-opened window, none opens nothing', async () => {
  const { ctrl, calls } = makeCopyRig();
  const win = { close: () => calls.push(['winClose']) };
  const here = await ctrl.copyProject({ id: 'p1', what: 'image', open: 'here' });
  assert.ok(calls.some(([k, id]) => k === 'loadProject' && id === here));
  const tab = await ctrl.copyProject({ id: 'p1', what: 'image', open: 'newtab', win });
  assert.deepEqual(calls.find(([k]) => k === 'newtab'), ['newtab', tab, win]);
  await ctrl.copyProject({ id: 'p1', what: 'image', open: 'none', win });
  assert.ok(calls.some(([k]) => k === 'winClose'), 'an unused pre-opened tab is closed');
});

test('incognito writes no row: here loads the copy into an incognito editor, a new tab launches it', async () => {
  const { ctrl, store, calls, storage } = makeCopyRig();
  const before = store.list().length;
  assert.equal(await ctrl.copyProject({ id: 'p1', what: 'layout', open: 'here', incognito: true }), null);
  assert.equal(storage.incognito, true);
  const load = calls.find(([k]) => k === 'load');
  assert.equal(load[1], 'Cat-copy.png');
  assert.equal(load[2].adoptLayout, true);
  await ctrl.copyProject({ id: 'p1', what: 'image', open: 'newtab', incognito: true });
  const launch = calls.find(([k]) => k === 'launch')[1];
  assert.deepEqual([launch.incognito, launch.dataUrl, launch.layout.lines], [true, IMG, []]);
  assert.equal(store.list().length, before);
});

test('a server-linked source copies onto its server, numbered past the server names too', async () => {
  const conn = makeServer('https://srv.example', ['Cat-copy']);
  const { ctrl, store, calls } = makeCopyRig({ metas: [{ ...META, address: conn.url, remoteId: 'r1' }], conn });
  const before = store.list().length;
  const remoteId = await ctrl.copyProject({ id: 'p1', what: 'project', open: 'here' });
  assert.equal(remoteId, 'r9');
  assert.equal(store.list().length, before, 'no local row');
  const create = conn.calls.find(([k]) => k === 'createProject')[1];
  assert.equal(create.name, 'Cat-copy(1)');
  assert.deepEqual([create.color, create.description, create.keywords], ['#112233', 'a cat', ['pet']]);
  assert.ok(conn.calls.some(([k, , kind]) => k === 'putFile' && kind === 'original'));
  assert.ok(conn.calls.some(([k]) => k === 'updateProject'), 'the layout is saved back');
  assert.deepEqual(calls.find(([k]) => k === 'chat')[2], { conn, remoteId: 'r9' });
  assert.deepEqual(calls.find(([k]) => k === 'openRemote')[1], { id: 'r9', serverUrl: conn.url, name: 'Cat-copy(1)' });
});

test('local:true keeps a server-linked source in this browser', async () => {
  const conn = makeServer();
  const { ctrl, store } = makeCopyRig({ metas: [{ ...META, address: conn.url, remoteId: 'r1' }], conn });
  const id = await ctrl.copyProject({ id: 'p1', what: 'image', local: true });
  assert.equal(store.getMeta(id).remoteId, null);
  assert.equal(conn.calls.length, 0);
});

test('a server-only row is fetched, then copied like any other source', async () => {
  const conn = makeServer();
  conn.getProject = async () => ({ project: { name: 'Remote', color: '#445566' }, layout: { imageWidth: 4, imageHeight: 3, lines: [] } });
  const { ctrl, store } = makeCopyRig({ conn });
  const readAsDataURL = globalThis.FileReader;
  globalThis.FileReader = class { readAsDataURL() { this.result = IMG; queueMicrotask(() => this.onload()); } };
  try {
    const id = await ctrl.copyProject({ remote: { id: 'r1', serverUrl: conn.url, name: 'Remote' }, what: 'project', local: true });
    assert.equal(store.getMeta(id).name, 'Remote-copy');
    assert.equal(store.getMeta(id).color, '#445566');
  } finally { globalThis.FileReader = readAsDataURL; }
});

test('a server-only row copies onto its server from the fetched original, never a re-decoded one', async () => {
  const conn = makeServer();
  conn.getProject = async () => ({ project: { name: 'Remote' }, layout: { imageWidth: 4, imageHeight: 3, lines: [{ points: [] }] } });
  const { ctrl, store } = makeCopyRig({ conn });
  const before = store.list().length;
  const readAsDataURL = globalThis.FileReader;
  globalThis.FileReader = class { readAsDataURL() { this.result = IMG; queueMicrotask(() => this.onload()); } };
  try {
    const remoteId = await ctrl.copyProject({ remote: { id: 'r1', serverUrl: conn.url, name: 'Remote' }, what: 'layout' });
    assert.equal(remoteId, 'r9');
    assert.equal(store.list().length, before, 'no local row');
    const [, , , meta, bytes] = conn.calls.find(([k, , kind]) => k === 'putFile' && kind === 'original');
    assert.deepEqual([...bytes], [1], 'the bytes the server sent, not the data URL decoded back');
    assert.equal(meta.ext, 'png');
    assert.ok(conn.calls.some(([k]) => k === 'updateProject'), 'the layout is saved back');
  } finally { globalThis.FileReader = readAsDataURL; }
});

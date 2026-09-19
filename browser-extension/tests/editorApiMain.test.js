// The editor page's stencil.extension API (src/content/editorApiMain.js): what it defines on the
// page, and the calls it posts at the bridge — editors, current, focus, tabs, images and open.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MSG, SRC, answer, calls, lastCall, loadApi, refuse, scanRow } from './helpers/editorApiEnv.js';

test('defines a guarded, non-enumerable window.__stencilExt', async () => {
  const env = await loadApi();
  const { ext, win } = env;
  assert.ok(ext, 'window.__stencilExt is defined');
  assert.equal(ext.__stencil, 'editor');

  const desc = Object.getOwnPropertyDescriptor(win, '__stencilExt');
  assert.equal(desc.enumerable, false);
  assert.equal(desc.writable, false);      // plain reassignment can't replace the binding
  assert.equal(win.stencil, undefined);    // the editor's own facade is never touched

  assert.throws(() => { ext.open = 0; }, /read-only/);
  assert.throws(() => { ext.__stencil = 'x'; }, /read-only/);
  assert.throws(() => { delete ext.editors; }, /cannot be deleted/);
});

test('a second inject leaves the existing API in place', async () => {
  const prior = { __stencil: 'editor' };
  const { win } = await loadApi({ extPreset: prior });
  assert.equal(win.__stencilExt, prior);
});

test('editors() posts EDITOR_LIST and resolves the rows', async () => {
  const env = await loadApi();
  const rows = [{ tabId: 3, projectName: 'Sketch', current: true }];

  const p = env.ext.editors();
  const msg = answer(env, { ok: true, editors: rows });
  assert.deepEqual(msg, { type: MSG.EDITOR_LIST, thumbnails: true });
  assert.deepEqual(await p, rows);

  const p2 = env.ext.editors({ thumbnails: false });   // cheap poll refresh: no canvas capture
  answer(env, { ok: true, editors: [] });
  assert.deepEqual(await p2, []);
  assert.equal(lastCall(env.posted).message.thumbnails, false);
});

test('current asks for THIS tab: an EDITOR_STATE call with no tabId', async () => {
  const env = await loadApi();
  const state = { projectId: 'p1', projectName: 'Here', hasImage: false };

  const p = env.ext.current;
  const msg = answer(env, { ok: true, state });
  assert.deepEqual(msg, { type: MSG.EDITOR_STATE });   // no tabId = the bridge answers locally
  assert.deepEqual(await p, state);
});

test('focus(), switchProject() and tabs() post their messages and unwrap the answer', async () => {
  const env = await loadApi();

  const focused = env.ext.focus(9);
  assert.deepEqual(answer(env, { ok: true, tabId: 9, windowId: 2 }), { type: MSG.EDITOR_FOCUS_TAB, tabId: 9 });
  assert.deepEqual(await focused, { tabId: 9, windowId: 2 });

  const switched = env.ext.switchProject('p2', { tabId: 9 });
  assert.deepEqual(answer(env, { ok: true, projectId: 'p2', projectName: 'Other' }),
    { type: MSG.EDITOR_SWITCH_PROJECT, projectId: 'p2', tabId: 9 });
  assert.deepEqual(await switched, { projectId: 'p2', projectName: 'Other' });

  // No tabId = this tab; the key is omitted entirely so the SW fills it from the sender.
  const here = env.ext.switchProject('p3');
  assert.deepEqual(answer(env, { ok: true, projectId: 'p3', projectName: 'Third' }),
    { type: MSG.EDITOR_SWITCH_PROJECT, projectId: 'p3' });
  await here;

  const tabs = env.ext.tabs();
  assert.deepEqual(answer(env, { ok: true, tabs: [{ tabId: 4, host: 'site.example' }] }),
    { type: MSG.SOURCE_TABS, currentWindowOnly: false });
  assert.deepEqual((await tabs).map((t) => t.tabId), [4]);
});

test('images() scans another tab and returns guarded entries carrying their provenance', async () => {
  const env = await loadApi();
  const p = env.ext.images(4, { limit: 50 });
  assert.deepEqual(answer(env, { ok: true, tabId: 4, url: 'http://site.example/post', images: [scanRow('http://cdn/a.png'), scanRow('', { kind: 'video', videoUrl: 'http://cdn/clip.mp4' })] }),
    { type: MSG.SCAN_TAB, tabId: 4, limit: 50 });

  const items = await p;
  assert.equal(items.length, 2);
  assert.equal(items[0].name, 'a.png');
  assert.equal(items[0].tabId, 4);
  assert.equal(items[0].resource, 'http://site.example/post');
  assert.equal(items[1].name, 'clip.mp4');       // a video is named from its media URL
  assert.throws(() => { items[0].src = 'x'; }, /read-only/);
});

// Calls whose ENVELOPE is the assertion never get answered; mocked timers keep their pending
// promise from timing out (and rejecting) after the test has ended.
test('open() imports into this tab: the resolved row, its provenance, and mode "ask" by default', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const env = await loadApi();
  const p = env.ext.open('http://cdn/a.png');
  const msg = answer(env, { ok: true, tabId: 3, mode: 'new', projectId: 'p9', projectName: 'a' });

  assert.deepEqual(msg, {
    type: MSG.EDITOR_IMPORT,
    image: { name: 'a.png', kind: 'img', src: 'http://cdn/a.png', source: 'http://cdn/a.png' },
    resource: '', mode: 'ask', incognito: false,
  });
  assert.deepEqual(await p, { tabId: 3, mode: 'new', projectId: 'p9', projectName: 'a' });

  // An explicit destination + mode + page/crop ride through untouched.
  const crop = { x: 1, y: 2, width: 3, height: 4 };
  env.ext.open('http://cdn/b.png', { tabId: 7, mode: 'replace-keep', page: 'A4', crop, incognito: true });
  const opts = lastCall(env.posted).message;
  assert.equal(opts.tabId, 7);
  assert.equal(opts.mode, 'replace-keep');
  assert.equal(opts.page, 'A4');
  assert.equal(opts.incognito, true);
  assert.deepEqual(opts.crop, crop);
});

test('open() resolves an images() index or a scanned entry, keeping the scanned page as the resource', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const env = await loadApi();
  const scanned = env.ext.images(4);
  answer(env, { ok: true, tabId: 4, url: 'http://site.example/post', images: [scanRow('http://cdn/a.png'), scanRow('', { kind: 'video', videoUrl: 'http://cdn/clip.mp4', posterUrl: 'http://cdn/p.jpg' })] });
  const items = await scanned;

  env.ext.open(0);                                   // index into the last images() result
  let msg = lastCall(env.posted).message;
  assert.deepEqual(msg.image, { name: 'a.png', kind: 'img', src: 'http://cdn/a.png', videoUrl: '', source: 'http://cdn/a.png' });
  assert.equal(msg.resource, 'http://site.example/post');

  env.ext.open(items[1]);                            // the entry itself; a video sources its media URL
  msg = lastCall(env.posted).message;
  assert.equal(msg.image.kind, 'video');
  assert.equal(msg.image.source, 'http://cdn/clip.mp4');

  env.ext.open({ kind: 'img', src: 'http://cdn/raw.png' });   // a plain row from elsewhere
  assert.equal(lastCall(env.posted).message.image.src, 'http://cdn/raw.png');
});

test('open() throws on a target it cannot address', async () => {
  const { ext } = await loadApi();
  assert.throws(() => ext.open(0), /call stencil.extension.images\(tabId\) first/);
  assert.throws(() => ext.open({}), /pass a scanned image entry/);
  assert.throws(() => ext.open(null), /pass a scanned image entry/);
  assert.throws(() => ext.open({ kind: 'video', videoUrl: '', src: '' }), /pass a scanned image entry/);
  assert.throws(() => ext.focus('3'), /pass the tabId of an open tab/);
});

test('a refusal rejects with the extension’s own message, carrying the chooser context', async () => {
  const env = await loadApi();
  const p = env.ext.open('http://cdn/a.png');
  const state = { hasImage: true, projectName: 'Busy' };
  refuse(env, 'editor already holds an image', { ok: false, error: 'editor already holds an image', needsChoice: true, state });

  const err = await p.then(() => null, (e) => e);
  assert.match(err.message, /editor already holds an image/);
  assert.equal(err.needsChoice, true);
  assert.deepEqual(err.state, state);
});

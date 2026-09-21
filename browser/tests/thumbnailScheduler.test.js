import { test, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import { createThumbnailScheduler, makeThumbnail, renderThumbnail } from '../js/core/image/thumbnail.js';
import { buildProjectMeta } from '../js/core/project/projectMeta.js';
import { ProjectsStore } from '../js/core/project/projectsStore.js';
import { createMemoryStorage } from './helpers/memoryStorage.js';

// A document whose canvases encode "what was drawn" into the data URL, so a render can be
// told apart from the last one without pixels. Node has no Worker → the inline path runs.
const savedDocument = globalThis.document;
const fakeDocument = () => ({
  createElement: () => {
    const c = { width: 0, height: 0, drawn: null };
    c.getContext = () => ({ drawImage(src) { c.drawn = src.tag || src.drawn; } });
    c.toDataURL = (type, q) => `data:${type};q=${q},${c.width}x${c.height},${c.drawn}`;
    return c;
  },
});
afterEach(() => { globalThis.document = savedDocument; });

const makeApp = (tag = 'v1') => {
  const app = { image: {}, tabs: { projectsChanged() { app.broadcasts++; } }, broadcasts: 0 };
  app.renderResultCanvas = () => ({ width: 1000, height: 500, tag: app.tag });
  app.tag = tag;
  return app;
};

// An idle queue the test drives by hand: `idle()` returns a handle, `runIdle()` fires them.
const manualIdle = () => {
  const queue = new Map();
  let n = 0;
  return {
    idle: (fn) => { const h = { id: ++n }; queue.set(h.id, fn); return h; },
    cancel: (h) => { queue.delete(h.id); },
    runIdle: async () => { const fns = [...queue.values()]; queue.clear(); for (const fn of fns) await fn(); },
    size: () => queue.size,
  };
};

const makeIo = (app) => {
  const store = new ProjectsStore(createMemoryStorage());
  store.upsert({ id: 'p1', name: 'one', thumbnail: null, hasImage: true }, { image: null, layout: {} });
  return { app, store, activeId: 'p1', temporary: false, incognito: false };
};

test('makeThumbnail renders the edited result at 480px JPEG 0.85, halving on the way', () => {
  globalThis.document = fakeDocument();
  assert.equal(makeThumbnail(makeApp()), 'data:image/jpeg;q=0.85,480x240,v1');
  assert.equal(makeThumbnail({ image: null }), null);
});

test('renderThumbnail is the same render, async (inline here — node has no Worker)', async () => {
  globalThis.document = fakeDocument();
  assert.equal(await renderThumbnail(makeApp('v2')), 'data:image/jpeg;q=0.85,480x240,v2');
  assert.equal(await renderThumbnail({ image: null }), null);
});

test('a burst of saves renders ONE thumbnail, in idle time, landing through setThumbnail', async () => {
  globalThis.document = fakeDocument();
  const q = manualIdle();
  const io = makeIo(makeApp('a'));
  const thumbs = createThumbnailScheduler(io, q);
  thumbs.schedule('p1'); thumbs.schedule('p1'); thumbs.schedule('p1');
  assert.equal(q.size(), 1, 'reschedules cancel the pending slot');
  assert.equal(thumbs.pending(), true);
  assert.equal(io.store.getMeta('p1').thumbnail, null, 'nothing rendered inline');
  await q.runIdle();
  assert.equal(io.store.getMeta('p1').thumbnail, 'data:image/jpeg;q=0.85,480x240,a');
  assert.equal(io.app.broadcasts, 1, 'other tabs get a list refresh');
  assert.equal(thumbs.pending(), false);
});

test('flush renders NOW, inline, and cancels the idle slot', () => {
  globalThis.document = fakeDocument();
  const q = manualIdle();
  const io = makeIo(makeApp('now'));
  const thumbs = createThumbnailScheduler(io, q);
  thumbs.schedule('p1');
  thumbs.flush();
  assert.equal(q.size(), 0);
  assert.equal(io.store.getMeta('p1').thumbnail, 'data:image/jpeg;q=0.85,480x240,now');
  thumbs.flush();   // idle: a no-op
  assert.equal(io.app.broadcasts, 1);
});

test('a render for a project that is no longer active is dropped — even one already in flight', async () => {
  globalThis.document = fakeDocument();
  const q = manualIdle();
  const io = makeIo(makeApp('old'));
  const thumbs = createThumbnailScheduler(io, q);
  thumbs.schedule('p1');
  io.activeId = 'p2';                       // switched before the idle slot came
  await q.runIdle();
  assert.equal(io.store.getMeta('p1').thumbnail, null);
  // In flight: the render started for p1, then a flush for a newer state landed first.
  io.activeId = 'p1';
  thumbs.schedule('p1');
  const slow = io.app.renderResultCanvas;
  io.app.renderResultCanvas = () => ({ width: 1000, height: 500, tag: 'stale' });
  const running = q.runIdle();              // awaits renderThumbnail (a microtask hop)
  io.app.renderResultCanvas = slow;
  thumbs.schedule('p1');
  thumbs.flush();                           // lands 'old' now
  await running;
  assert.equal(io.store.getMeta('p1').thumbnail, 'data:image/jpeg;q=0.85,480x240,old', 'the stale in-flight render did not overwrite the flush');
});

test('buildProjectMeta takes the thumbnail the save path hands it, else renders inline', () => {
  globalThis.document = fakeDocument();
  const app = { ...makeApp('meta'), canvas: { width: 1000, height: 500 }, imageDataUrl: 'data:x', lines: [] };
  const layout = { lines: [] };
  assert.equal(buildProjectMeta(app, { id: 'p', layout, thumbnail: 'kept' }).thumbnail, 'kept');
  assert.equal(buildProjectMeta(app, { id: 'p', layout, thumbnail: null }).thumbnail, null);
  assert.equal(buildProjectMeta(app, { id: 'p', layout }).thumbnail, 'data:image/jpeg;q=0.85,480x240,meta');
});

test('ProjectsStore.setThumbnail patches the row only — payload and updatedAt untouched', () => {
  const store = new ProjectsStore(createMemoryStorage());
  store.upsert({ id: 'p1', name: 'one', thumbnail: null, updatedAt: 5 }, { image: 'img', layout: { a: 1 } });
  const before = store.getMeta('p1').updatedAt;
  assert.equal(store.setThumbnail('p1', 'data:t').thumbnail, 'data:t');
  assert.equal(store.getMeta('p1').thumbnail, 'data:t');
  assert.equal(store.getMeta('p1').updatedAt, before);
  assert.deepEqual(store.get('p1').payload, { image: 'img', layout: { a: 1 } });
  assert.equal(store.setThumbnail('nope', 'x'), null);
});

// The webcore toggle (js/ui/webcore/toggle.js + scene.js): on, the page is stamped and still
// without a store write and keeps the theme it was wearing, the empty editor gets the picture,
// the word and the name; off, the stored look is back. The trigger reaches it by the typed word.
import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';

let doc, store, toasts;
beforeEach(() => {
  store = new Map();
  globalThis.localStorage = { getItem: (k) => store.get(k) ?? null, setItem: (k, v) => store.set(k, String(v)), removeItem: (k) => store.delete(k) };
  toasts = [];
  const balloon = createStubElement('stencil-notifications', { notify: (msg, type, opts) => toasts.push({ msg, type, opts }) });
  const link = createStubElement('link');
  doc = installDom({
    body: createStubElement('body'),
    getElementById: (id) => (id === 'notify-balloon' ? balloon : null),
    querySelector: (sel) => (sel === 'link[rel="icon"]' ? link : null),
  });
  doc.link = link;
  doc.documentElement.setAttribute('data-theme', 'dark');
  globalThis.matchMedia = () => ({ matches: false });
  globalThis.requestAnimationFrame = (fn) => setTimeout(fn, 0);
  globalThis.File = class { constructor(parts, name, opts) { this.name = name; this.type = opts?.type; } };
  doc.createElement = (tag) => (tag === 'canvas'
    ? createStubElement('canvas', { getContext: () => ({ fillRect() {} }), toBlob: (cb) => cb({}) })
    : createStubElement(tag));
});

const { toggleWebcore, webcoreActive } = await import('../../../js/ui/webcore/toggle.js');
const { motionPrefs, motionReduced, setMotionPrefs, reloadMotionPrefs, MOTION_STORAGE_KEY } = await import('../../../js/ui/motion/motionPrefs.js');
const { AccentController, THEME_STORAGE_KEY } = await import('../../../js/ui/accent/controller.js');
const { activateShow } = await import('../../../js/ui/logo/stageTrigger.js');
const { WEBCORE } = await import('../../../js/ui/webcore/rules.js');

const makeApp = (over = {}) => {
  const app = {
    image: { width: 1024, height: 768 }, accent: 'violet', customAccent: null,
    storage: { incognito: false }, calls: [],
    loadImageFromFile(file) { app.calls.push(['load', file.name]); app.image = { width: 1024, height: 768 }; app.imageBaseName = 'webcore'; },
    updateIncognitoUI() { app.calls.push(['incognitoUI']); },
    export: { installLayout: (data, opts) => { app.calls.push(['layout', data, opts]); return true; } },
    settings: { setImageFilter: (v) => app.calls.push(['filter', v]) },
    ...over,
  };
  app.accents = new AccentController(app);
  Object.defineProperty(app, 'theme', { get: () => doc.documentElement.getAttribute('data-theme') });
  return app;
};

test('on: stamped, pixel-drawn in the theme it was wearing, still — and nothing is stored', async () => {
  store.set(THEME_STORAGE_KEY, 'dark');
  reloadMotionPrefs();
  const app = makeApp();
  assert.equal(await toggleWebcore(app), true);
  assert.equal(webcoreActive(), true);
  assert.equal(doc.documentElement.getAttribute('data-skin'), 'webcore');
  assert.equal(doc.documentElement.getAttribute('data-theme'), 'dark', 'the chosen theme stands');
  assert.equal(motionReduced(), true, 'the interface stops moving');
  assert.equal(motionPrefs().drawing, false, 'the lines stop animating');
  assert.equal(store.get(THEME_STORAGE_KEY), 'dark', 'the stored theme is untouched');
  assert.equal(store.has(MOTION_STORAGE_KEY), false, 'the stored motion is untouched');
  assert.match(doc.link.href, /^data:image\/svg\+xml,/);
  assert.equal(app.calls.length, 0, 'an editor with a picture keeps it');
  // Off: the stored look, the line-art, the accent favicon.
  assert.equal(await toggleWebcore(app), false);
  assert.equal(doc.documentElement.hasAttribute('data-skin'), false);
  assert.equal(doc.documentElement.getAttribute('data-theme'), 'dark');
  assert.equal(motionPrefs().mode, 'particles', 'the stored motion is back');
  assert.equal(motionPrefs().drawing, true);
  assert.equal(store.has(MOTION_STORAGE_KEY), false);
});

test('a motion choice made while it is on persists, and the skin stays', async () => {
  reloadMotionPrefs();
  const app = makeApp();
  await toggleWebcore(app);
  setMotionPrefs({ mode: 'water' });
  assert.equal(motionPrefs().mode, 'water');
  assert.equal(motionPrefs().drawing, true);
  assert.equal(JSON.parse(store.get(MOTION_STORAGE_KEY)).mode, 'water');
  assert.equal(webcoreActive(), true);
  await toggleWebcore(app);
  assert.equal(motionPrefs().mode, 'water', 'off keeps what the user chose');
});

test('an empty editor leaves incognito, gets the picture under its name and the word as one step', async () => {
  reloadMotionPrefs();
  const app = makeApp({ image: null, storage: { incognito: true }, imageFilter: 'sepia' });
  await toggleWebcore(app);
  assert.equal(app.storage.incognito, false);
  assert.deepEqual(app.calls[0], ['incognitoUI']);
  assert.deepEqual(app.calls[1], ['filter', 'none']);
  assert.deepEqual(app.calls[2], ['load', WEBCORE.strings.imageName]);
  const [, data, opts] = app.calls[3];
  assert.deepEqual(opts, { mode: 'combine', history: true });
  assert.equal(data.lines.length, 7);
  assert.equal(data.imageWidth, 1024);
  await toggleWebcore(app);
});

test('an empty editor reopens the local project named webcore instead of minting another', async () => {
  reloadMotionPrefs();
  const list = () => [
    { id: 'srv', name: 'webcore', remoteId: 'r1' },
    { id: 'loc', name: 'WebCore ' },
    { id: 'two', name: 'webcore (2)' },
  ];
  const opened = [];
  const app = makeApp({ image: null, storage: { incognito: false, store: { list } },
                        switchToProject: (id) => { opened.push(id); return true; } });
  await toggleWebcore(app);
  assert.deepEqual(opened, ['loc'], 'the local one, never the server-linked copy');
  assert.equal(app.calls.some(([k]) => k === 'load'), false, 'no new picture, no new project');
  await toggleWebcore(app);
});

test('the typed word reaches it through the trigger, with the gold notice each way', async () => {
  reloadMotionPrefs();
  const app = makeApp();
  assert.equal(await activateShow('webcore', app), true);
  assert.equal(toasts.at(-1).msg, 'Secret activated');
  assert.equal(toasts.at(-1).opts.shine, true);
  assert.equal(await activateShow('webcore', app), false);
  assert.equal(toasts.at(-1).msg, WEBCORE.strings.off);
  assert.equal(toasts.length, 2);
});

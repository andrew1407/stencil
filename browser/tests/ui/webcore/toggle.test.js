// The webcore toggle (js/ui/webcore/toggle.js + scene.js): on, the page is stamped, and still and
// Gray become the user's stored settings, keeping the theme it was wearing; the empty editor gets the
// picture, the word and the name; off, what they replaced is back. The trigger reaches it by the typed word.
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
const { ACCENT_STORAGE_KEY } = await import('../../../js/core/settings/accents.js');
const { activateShow, restoreSkin } = await import('../../../js/ui/logo/stageTrigger.js');
const { SKIN_STORAGE_KEY } = await import('../../../js/ui/skin/stored.js');
const { WEBCORE } = await import('../../../js/ui/webcore/rules.js');

// The scene loads through the real core/image/loadFlow.js; the browser's decode is the boundary:
// the reader records the file it was handed, and the decoded original stands in for the settle.
const stubDecode = (t, app) => {
  const saved = { FileReader: globalThis.FileReader, Image: globalThis.Image };
  t.after(() => Object.assign(globalThis, saved));
  globalThis.FileReader = class {
    readAsDataURL(file) { app.calls.push(['load', file.name]); this.onload({ target: { result: 'data:image/png;base64,' } }); }
  };
  globalThis.Image = class { set src(_) { this.onload(); app.image = { width: 1024, height: 768 }; } };
};

const tick = () => new Promise((resolve) => setTimeout(resolve, 0));

const makeApp = (over = {}) => {
  const app = {
    image: { width: 1024, height: 768 }, accent: 'violet', customAccent: null,
    storage: { incognito: false }, calls: [], promoted: [],
    tabs: { reportActive: (id) => app.promoted.push(['reportActive', id]), broadcastAccent() {} },
    updateIncognitoUI() { app.calls.push(['incognitoUI']); },
    export: { installLayout: (data, opts) => { app.calls.push(['layout', data, opts]); return true; } },
    settings: { setImageFilter: (v) => app.calls.push(['filter', v]) },
    ...over,
  };
  app.accents = new AccentController(app);
  Object.defineProperty(app, 'theme', { get: () => doc.documentElement.getAttribute('data-theme') });
  // The app reads its accent off <html data-accent>, as DrawingApp does.
  doc.documentElement.setAttribute('data-accent', app.accent);
  Object.defineProperty(app, 'accent', { get: () => doc.documentElement.getAttribute('data-accent') });
  return app;
};

test('on: stamped, pixel-drawn in the theme it was wearing; still and Gray stored as the user\'s own', async () => {
  store.set(THEME_STORAGE_KEY, 'dark');
  reloadMotionPrefs();
  const app = makeApp();
  assert.equal(await toggleWebcore(app), true);
  assert.equal(webcoreActive(), true);
  assert.equal(doc.documentElement.getAttribute('data-skin'), 'webcore');
  assert.equal(doc.documentElement.getAttribute('data-theme'), 'dark', 'the chosen theme stands');
  assert.equal(motionReduced(), true, 'the interface stops moving');
  assert.equal(motionPrefs().drawing, false, 'the lines stop animating');
  await tick();   // the accent pours a frame later
  assert.equal(store.get(THEME_STORAGE_KEY), 'dark', 'the stored theme is untouched');
  assert.deepEqual(JSON.parse(store.get(MOTION_STORAGE_KEY)).mode, 'none', 'still is stored: a relaunch keeps it');
  assert.equal(JSON.parse(store.get(MOTION_STORAGE_KEY)).drawing, false);
  assert.equal(app.accent, 'grey', 'the word sets Gray');
  assert.equal(store.get(ACCENT_STORAGE_KEY), 'grey');
  assert.equal(store.get(SKIN_STORAGE_KEY), 'webcore', 'the skin itself is kept for the next launch');
  assert.match(doc.link.href, /^data:image\/svg\+xml,/);
  assert.equal(app.calls.length, 0, 'an editor with a picture keeps it');
  // Off: what the word replaced, the line-art, the accent favicon.
  assert.equal(await toggleWebcore(app), false);
  await tick();
  assert.equal(doc.documentElement.hasAttribute('data-skin'), false);
  assert.equal(doc.documentElement.getAttribute('data-theme'), 'dark');
  assert.equal(motionPrefs().mode, 'particles', 'the motion it replaced is back');
  assert.equal(motionPrefs().drawing, true);
  assert.equal(JSON.parse(store.get(MOTION_STORAGE_KEY)).mode, 'particles');
  assert.equal(app.accent, 'violet', 'and the accent');
  assert.equal(store.get(ACCENT_STORAGE_KEY), 'violet');
  assert.equal(store.has(SKIN_STORAGE_KEY), false, 'off forgets it');
});

test('a relaunch re-wears the stored skin over the stored settings, sceneless and silent', async () => {
  store.set(MOTION_STORAGE_KEY, JSON.stringify({ mode: 'water' }));
  reloadMotionPrefs();
  const app = makeApp({ image: null });
  store.set(SKIN_STORAGE_KEY, 'bogus');
  assert.equal(await restoreSkin(app), false);
  store.set(SKIN_STORAGE_KEY, 'webcore');
  assert.equal(await restoreSkin(app), true);
  assert.equal(webcoreActive(), true);
  assert.equal(motionPrefs().mode, 'water', 'what is stored stands: a relaunch sets nothing');
  assert.equal(app.accent, 'violet');
  assert.equal(app.calls.length, 0, 'an empty editor gets no scene');
  assert.equal(toasts.length, 0, 'and no toast');
  assert.equal(await restoreSkin(app), false, 'a second restore leaves it on');
  await toggleWebcore(app);
  assert.equal(await restoreSkin(app), false, 'switched off, nothing comes back');
  assert.equal(motionPrefs().mode, 'water', 'and off after a relaunch keeps the stored settings');
});

test('the word\'s look survives the relaunch: still and Gray are what the next launch stores', async () => {
  reloadMotionPrefs();
  const app = makeApp();
  await toggleWebcore(app);
  await tick();
  doc.documentElement.removeAttribute('data-skin');   // the page closes with the skin on
  reloadMotionPrefs();                                 // and the relaunch reads the store again
  const next = makeApp();
  assert.equal(await restoreSkin(next), true);
  assert.equal(motionPrefs().mode, 'none');
  assert.equal(motionPrefs().drawing, false);
  assert.equal(store.get(ACCENT_STORAGE_KEY), 'grey');
  await toggleWebcore(next);
  assert.equal(motionPrefs().mode, 'none', 'off after a relaunch keeps them: they are the user\'s now');
});

test('a motion choice made while it is on persists, and the skin stays', async () => {
  reloadMotionPrefs();
  const app = makeApp();
  await toggleWebcore(app);
  setMotionPrefs({ mode: 'water' });
  assert.equal(motionPrefs().mode, 'water');
  assert.equal(JSON.parse(store.get(MOTION_STORAGE_KEY)).mode, 'water');
  assert.equal(webcoreActive(), true);
  await toggleWebcore(app);
  assert.equal(motionPrefs().mode, 'water', 'off keeps what the user chose');
  assert.equal(motionPrefs().drawing, true, 'and gives back the switch they left alone');
});

test('an empty editor leaves incognito, gets the picture under its name and the word as one step', async (t) => {
  reloadMotionPrefs();
  const storage = { incognito: true, temporary: true };
  // The settle runs as far as the word: a crop, the history's floor, then the layout as its first step.
  const imageModel = { defaultCropRect: () => null, rebuildCroppedImage() {} };
  const app = makeApp({ image: null, storage, imageFilter: 'sepia', activeProjectId: null,
                        imageModel, history: { reset() {} } });
  storage.promoteTemporaryToProject = () => { app.promoted.push(['promote']); app.activeProjectId = 'p1'; };
  stubDecode(t, app);
  await toggleWebcore(app);
  assert.equal(app.storage.incognito, false);
  assert.deepEqual(app.calls[0], ['incognitoUI']);
  assert.deepEqual(app.calls[1], ['filter', 'none']);
  assert.deepEqual(app.calls[2], ['load', WEBCORE.strings.imageName]);
  assert.deepEqual(app.promoted, [['promote'], ['reportActive', 'p1']], 'out of incognito, the picture becomes a project');
  assert.equal(`${app.imageBaseName}.${app.imageExt}`, WEBCORE.strings.imageName.toLowerCase(), 'named by its file');
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
                        projectTransfer: { switchToProject: (id) => { opened.push(id); setTimeout(() => { app.image = { width: 1024, height: 768 }; }); return true; } } });
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

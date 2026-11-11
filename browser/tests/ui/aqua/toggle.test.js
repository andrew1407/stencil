// The aqua toggle (js/ui/aqua/toggle.js + scene.js) and its trigger: on, the page is stamped, Blue
// and Sliding are stored as the user's own, the gel wears the accent, and the empty editor gets its scene;
// the trigger keeps one skin at a time, and the mark's hold opens it only blue, sliding, light.
import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';

let doc, store, toasts;
beforeEach(() => {
  globalThis.window ??= new EventTarget();   // the bus the accent change rides
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
  doc.documentElement.setAttribute('data-theme', 'light');
  globalThis.matchMedia = () => ({ matches: false });
  globalThis.requestAnimationFrame = (fn) => setTimeout(fn, 0);
  globalThis.File = class { constructor(parts, name, opts) { this.name = name; this.type = opts?.type; } };
  doc.createElement = (tag) => (tag === 'canvas'
    ? createStubElement('canvas', { getContext: () => ({ fillRect() {} }), toBlob: (cb) => cb({}) })
    : createStubElement(tag));
});

const { toggleAqua, aquaActive } = await import('../../../js/ui/aqua/toggle.js');
const { webcoreActive } = await import('../../../js/ui/webcore/toggle.js');
const { motionPrefs, setMotionPrefs, reloadMotionPrefs, MOTION_STORAGE_KEY } = await import('../../../js/ui/motion/motionPrefs.js');
const { activateShow, heldShow, setShow, showActive, restoreSkin } = await import('../../../js/ui/logo/stageTrigger.js');
const { AQUA, gelTokens } = await import('../../../js/ui/aqua/rules.js');
const { publish: emit, EVENTS } = await import('../../../js/eventBus/appBus.js');
const { WEBCORE } = await import('../../../js/ui/webcore/rules.js');
const { SKIN_STORAGE_KEY } = await import('../../../js/ui/skin/stored.js');

const makeApp = (over = {}) => {
  const app = {
    image: { width: 1024, height: 768 }, accent: 'blue', customAccent: null,
    storage: { incognito: false }, calls: [],
    updateIncognitoUI() { app.calls.push(['incognitoUI']); },
    export: { installLayout: (data, opts) => { app.calls.push(['layout', data, opts]); return true; } },
    settings: { setImageFilter: (v) => app.calls.push(['filter', v]) },
    accents: {
      setAccent: (key, from, opts) => {
        app.calls.push(['accent', key, opts]); app.accent = key; app.customAccent = null; emit(EVENTS.accentChanged, key);
      },
      setCustomAccent: (hex) => { app.calls.push(['custom', hex]); app.customAccent = hex; emit(EVENTS.accentChanged, hex); },
    },
    ...over,
  };
  return app;
};

test('on: stamped, Blue and sliding stored as the user\'s, the gel and the mark; off gives back what they replaced', async () => {
  reloadMotionPrefs();
  const app = makeApp({ accent: 'violet' });
  const logo = createStubElement('svg');
  logo.innerHTML = '<rect/>';
  doc.body.querySelector = (sel) => (sel === 'svg.app-logo' ? logo : null);
  assert.equal(await toggleAqua(app), true);
  assert.equal(aquaActive(), true);
  assert.equal(doc.documentElement.getAttribute('data-skin'), 'aqua');
  assert.deepEqual(app.calls.shift(), ['accent', 'blue', undefined], 'the word sets Blue, stored as any pick is');
  assert.equal(doc.documentElement.style.getPropertyValue('--aq-gel-bottom'), '#2563eb', 'the gel is Blue');
  assert.equal(doc.documentElement.style.getPropertyValue('--aq-focus'), gelTokens('#2563eb')['--aq-focus']);
  assert.equal(motionPrefs().mode, 'slide', 'the interface slides');
  assert.equal(JSON.parse(store.get(MOTION_STORAGE_KEY)).mode, 'slide', 'and a relaunch keeps it');
  assert.match(logo.innerHTML, /stencilAquaGel/, 'the header mark wears the gel art');
  assert.match(logo.innerHTML, /#2563eb/, 'in the accent');
  emit(EVENTS.accentChanged, '#ff8800');
  assert.equal(doc.documentElement.style.getPropertyValue('--aq-gel-bottom'), '#ff8800', 'an accent change repaints the gel');
  assert.match(logo.innerHTML, /#ff8800/, 'and the mark');
  assert.equal(store.get(SKIN_STORAGE_KEY), 'aqua', 'the skin is stored for the next launch');
  assert.equal(app.calls.length, 0, 'an editor with a picture keeps it');
  assert.equal(await toggleAqua(app), false);
  assert.equal(doc.documentElement.hasAttribute('data-skin'), false);
  assert.equal(doc.documentElement.style.getPropertyValue('--aq-focus'), '');
  assert.equal(doc.documentElement.style.getPropertyValue('--aq-gel-bottom'), '');
  assert.equal(motionPrefs().mode, 'particles', 'the motion it replaced is back');
  assert.deepEqual(app.calls, [['accent', 'violet', undefined]], 'and the accent');
  assert.equal(store.has(SKIN_STORAGE_KEY), false);
  assert.equal(logo.innerHTML, '<rect/>', 'and the app\'s own mark');
});

test('off in the same session gives the user\'s own accent back; a Blue user is left alone; their own pick stands', async () => {
  const app = makeApp({ accent: 'violet' });
  await toggleAqua(app);
  await toggleAqua(app);
  assert.deepEqual(app.calls, [['accent', 'blue', undefined], ['accent', 'violet', undefined]]);
  const blue = makeApp({ accent: 'blue' });
  await toggleAqua(blue);
  await toggleAqua(blue);
  assert.deepEqual(blue.calls, []);
  const custom = makeApp({ accent: 'violet', customAccent: '#123456' });
  await toggleAqua(custom);
  await toggleAqua(custom);
  assert.deepEqual(custom.calls.at(-1), ['custom', '#123456']);
  const own = makeApp({ accent: 'violet' });
  await toggleAqua(own);
  own.accents.setAccent('grass');
  await toggleAqua(own);
  assert.deepEqual(own.calls.at(-1), ['accent', 'grass', undefined], 'a pick made under the skin stays');
  assert.equal(store.has(SKIN_STORAGE_KEY), false, 'off forgets the skin');
});

test('an empty editor reopens the local somefruitcompany project rather than mint another', async () => {
  const opened = [];
  const list = () => [{ id: 'srv', name: 'somefruitcompany', remoteId: 'r' }, { id: 'loc', name: 'SomeFruitCompany ' }];
  const app = makeApp({ image: null, storage: { incognito: false, store: { list } },
                        projectTransfer: { switchToProject: (id) => { opened.push(id); setTimeout(() => { app.image = { width: 1024, height: 768 }; }); return true; } } });
  await toggleAqua(app);
  assert.deepEqual(opened, ['loc']);
  await toggleAqua(app);
});

test('the idle card is held from the word until the picture lands, then given back', async () => {
  const wrap = createStubElement('div');
  doc.getElementById = (id) => (id === 'idle-create-wrap' ? wrap : null);
  let heldAtOpen = null;
  const list = () => [{ id: 'loc', name: 'somefruitcompany' }];
  const app = makeApp({ image: null, storage: { incognito: false, store: { list } },
                        projectTransfer: { switchToProject: () => {
                          heldAtOpen = wrap.classList.contains('idle-held');
                          setTimeout(() => { app.image = { width: 1024, height: 768 }; });
                          return true;
                        } } });
  await toggleAqua(app, doc);
  assert.equal(heldAtOpen, true, 'down before the skin can paint it');
  assert.equal(wrap.classList.contains('idle-held'), false, 'released once the picture is in');
  await toggleAqua(app, doc);
});

test('the trigger toasts each way and keeps one skin: the word for one turns the other off', async () => {
  reloadMotionPrefs();
  const app = makeApp();
  assert.equal(await activateShow('someFruitCompany', app), true);
  assert.equal(toasts.at(-1).msg, 'Secret activated');
  assert.equal(await activateShow('webcore', app), true);
  assert.equal(webcoreActive(), true);
  assert.equal(aquaActive(), false, 'webcore took the page');
  assert.equal(await activateShow('someFruitCompany', app), true);
  assert.equal(aquaActive(), true);
  assert.equal(webcoreActive(), false, 'and aqua took it back');
  assert.equal(motionPrefs().mode, 'slide', 'webcore\'s stillness left with it, aqua\'s slide came in');
  assert.equal(await activateShow('someFruitCompany', app), false);
  assert.equal(toasts.at(-1).msg, AQUA.strings.off);
  assert.notEqual(AQUA.strings.off, WEBCORE.strings.off);
});

test('the Mode switch is idempotent and says what is on', async () => {
  const app = makeApp();
  setShow('someFruitCompany', app, true);
  await new Promise((r) => setTimeout(r, 0));
  assert.equal(showActive('someFruitCompany', app), true);
  setShow('someFruitCompany', app, true);
  assert.equal(showActive('someFruitCompany', app), true);
  setShow('someFruitCompany', app, false);
  await new Promise((r) => setTimeout(r, 0));
  assert.equal(showActive('someFruitCompany', app), false);
});

test('the hold opens it only for blue, sliding, on the light face', () => {
  setMotionPrefs({ mode: 'slide' });
  const app = makeApp();
  assert.equal(heldShow(app), 'someFruitCompany');
  doc.documentElement.setAttribute('data-theme', 'dark');
  assert.equal(heldShow(app), null, 'graphite needs the word');
  doc.documentElement.setAttribute('data-theme', 'light');
  setMotionPrefs({ mode: 'water' });
  assert.equal(heldShow(app), 'waterShow', 'blue in water is still the water show');
  assert.equal(heldShow({ ...app, accent: 'sky' }), 'waterShow');
  setMotionPrefs({ mode: 'slide' });
  assert.equal(heldShow({ ...app, accent: 'sky' }), null, 'only the Blue preset');
  assert.ok(store.has(MOTION_STORAGE_KEY));
});

test('a relaunch re-wears the stored aqua in the user\'s own accent and motion: no Blue, no slide, no scene', async () => {
  reloadMotionPrefs();
  store.set(SKIN_STORAGE_KEY, 'aqua');
  const app = makeApp({ accent: 'violet', image: null });
  assert.equal(await restoreSkin(app), true);
  assert.equal(aquaActive(), true);
  assert.deepEqual(app.calls, [], 'no Blue is worn, and the empty editor gets no scene');
  assert.equal(motionPrefs().mode, 'particles', 'the user\'s own motion');
  assert.equal(doc.documentElement.style.getPropertyValue('--aq-gel-bottom'), '#7c3aed', 'the gel in the user\'s accent');
  assert.equal(toasts.length, 0);
  assert.equal(await toggleAqua(app), false);
  assert.deepEqual(app.calls, [], 'off gives nothing back that was never set aside');
  assert.equal(await restoreSkin(app), false, 'off stays off');
});

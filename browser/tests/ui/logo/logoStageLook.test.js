// A running show re-resolves its look live (js/ui/logo/stageLook.js): the webcore skin, the motion
// mode and the accent reach the open stage on its next frame, without closing or restarting it.
import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import { createStencil, makeApp } from '../../helpers/stencilApiRig.js';
import { installDom, createStubElement } from '../../helpers/dom.js';
import { currentLogoStage, closeLogoStage } from '../../../js/ui/logo/stage.js';
import { reloadMotionPrefs, setMotionPrefs } from '../../../js/ui/motion/motionPrefs.js';
import { webcoreActive } from '../../../js/ui/webcore/toggle.js';

let frames;
const tick = (n = 3) => { for (let i = 0; i < n; i++) frames.splice(0).forEach((fn) => fn()); };

beforeEach(() => {
  closeLogoStage();
  frames = [];
  const store = new Map();
  globalThis.localStorage = { getItem: (k) => store.get(k) ?? null, setItem: (k, v) => store.set(k, String(v)), removeItem: (k) => store.delete(k) };
  const notices = createStubElement('stencil-notifications', { notify() {} });
  installDom({ body: createStubElement('body'), getElementById: (id) => (id === 'notify-balloon' ? notices : null),
    querySelector: () => null, querySelectorAll: () => [] });
  globalThis.window = new EventTarget();
  globalThis.matchMedia = () => ({ matches: false });
  globalThis.innerWidth = 1000; globalThis.innerHeight = 600;
  let clock = 0;
  globalThis.performance = { now: () => (clock += 16) };
  globalThis.requestAnimationFrame = (fn) => { frames.push(fn); return frames.length; };
  globalThis.cancelAnimationFrame = () => {};
  reloadMotionPrefs();
});

const eggs = () => createStencil(makeApp()).EasterEggs;

test('webcore on stills the cloud of the open show, and off brings back the stored style', async () => {
  setMotionPrefs({ mode: 'water' });
  const e = eggs();
  e.randomWalk();
  const stage = currentLogoStage();
  tick();
  const plain = stage.markSrc;
  assert.ok(plain);
  assert.equal(stage.cloudStyle, 'water');
  assert.ok(stage.cloudLive > 0, 'grains fly as it opens');

  e.webcoreMode = true;
  await Promise.resolve();
  assert.equal(webcoreActive(), true);
  tick();
  assert.equal(currentLogoStage(), stage, 'the same stage, never reopened');
  assert.equal(stage.cloudStyle, null, 'the skin flies none');
  assert.equal(stage.cloudLive, 0, 'and no grain outlives it');
  assert.notEqual(stage.markSrc, plain, 'the mark wears the pixel art');
  assert.match(decodeURIComponent(stage.markSrc), /crispEdges|<rect/, 'drawn in pixels');

  e.webcoreMode = false;
  await Promise.resolve();
  assert.equal(webcoreActive(), false);
  tick();
  assert.equal(currentLogoStage(), stage, 'still the same stage');
  assert.equal(stage.cloudStyle, 'water', 'the stored style is back');
  assert.ok(stage.cloudLive > 0, 'and flying again');
  assert.equal(stage.markSrc, plain, 'the normal mark is back');
  closeLogoStage();
});

test('a motion change restyles the cloud live; a show with its own motion keeps it', () => {
  const e = eggs();
  e.randomWalk();
  const stage = currentLogoStage();
  tick();
  assert.equal(stage.cloudStyle, 'dust');
  setMotionPrefs({ mode: 'fire' });
  tick();
  assert.equal(stage.cloudStyle, 'fire');
  setMotionPrefs({ mode: 'slide' });
  tick();
  assert.equal(stage.cloudStyle, null, 'a sliding interface flies none');
  closeLogoStage();

  e.firework();
  const own = currentLogoStage();
  setMotionPrefs({ mode: 'none' });
  tick();
  assert.equal(own.cloudStyle, 'fire', 'its own motion, whatever the mode');
  closeLogoStage();
});

test('the mark is redrawn when its art or accent moves, and only then', async () => {
  const { createStageLook } = await import('../../../js/ui/logo/stageLook.js');
  const { setFaviconArt } = await import('../../../js/core/settings/accents.js');
  const app = { accent: 'violet', customAccent: null };
  const look = createStageLook('neonOn', app, globalThis.document);
  const first = look.img;
  look.refresh();
  assert.equal(look.img, first, 'nothing moved, nothing redrawn');
  setFaviconArt(() => '<svg id="skin"/>');
  window.dispatchEvent(new Event('stencil:theme-changed'));
  look.refresh();
  assert.notEqual(look.img, first, 'the skin swapped the art');
  assert.match(decodeURIComponent(look.img.src), /skin/);
  setFaviconArt(null);
  look.unwatch();
});

test('with no cloud flying only the light-only shows glow', async () => {
  const { createStageLook } = await import('../../../js/ui/logo/stageLook.js');
  const glows = (name) => { const l = createStageLook(name, null, globalThis.document); l.unwatch(); return l.glows; };
  setMotionPrefs({ mode: 'none' });
  for (const name of ['neonOn', 'makeSomeSunshine']) assert.equal(glows(name), true, name);
  for (const name of ['chaseMe', 'runaway', 'randomWalk', 'makeItSmall']) assert.equal(glows(name), false, name);
  setMotionPrefs({ mode: 'particles' });
  for (const name of ['chaseMe', 'runaway', 'randomWalk']) assert.equal(glows(name), true, `${name} with its cloud`);
});

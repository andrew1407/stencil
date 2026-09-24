// stencil.EasterEggs.<show>Mode — every show as an on/off switch, and a new show winning over
// the one up, an open window and fullscreen. webcoreMode dresses the page and never opens one.
import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import { createStencil, makeApp } from '../../helpers/stencilApiRig.js';
import { installDom, createStubElement } from '../../helpers/dom.js';
import { SHOW_NAMES, STAGE, effectOf } from '../../../js/ui/logo/stageRules.js';
import { currentLogoStage, closeLogoStage } from '../../../js/ui/logo/stage.js';
import { modalShells } from '../../../js/ui/modal/registry.js';
import { webcoreActive } from '../../../js/ui/webcore/toggle.js';

let doc, toasts, overlays;
beforeEach(() => {
  closeLogoStage();
  toasts = [];
  overlays = [];
  const balloon = createStubElement('stencil-notifications', { notify: (msg) => toasts.push(msg) });
  const store = new Map();
  globalThis.localStorage = { getItem: (k) => store.get(k) ?? null, setItem: (k, v) => store.set(k, String(v)), removeItem: (k) => store.delete(k) };
  const open = () => overlays.filter((o) => o.classList.contains('modal-open'));
  doc = installDom({
    body: createStubElement('body'),
    getElementById: (id) => (id === 'notify-balloon' ? balloon : null),
    querySelector: (sel) => (sel === '.app-modal-overlay.modal-open' ? open()[0] ?? null : null),
    querySelectorAll: (sel) => (sel === '.app-modal-overlay.modal-open' ? open() : []),
  });
  globalThis.matchMedia = () => ({ matches: false });
  globalThis.innerWidth = 1000; globalThis.innerHeight = 600;
  globalThis.requestAnimationFrame = () => 1;
  globalThis.cancelAnimationFrame = () => {};
  globalThis.performance = { now: () => 0 };
});

const eggsOf = (over) => { const app = makeApp(over); return { app, stencil: createStencil(app) }; };
const STAGED = SHOW_NAMES.filter((n) => !['pink', 'webcore'].includes(effectOf(n)));

test('every show has its switch, and what() still lists only the shows', () => {
  const { stencil } = eggsOf();
  for (const name of SHOW_NAMES) assert.equal(stencil.EasterEggs[`${name}Mode`], false, name);
  assert.deepEqual(stencil.EasterEggs.what(), SHOW_NAMES);
  assert.equal(stencil.EasterEggs.neononmode, false, 'a switch answers in any casing');
});

test('a stage switch opens, reads back, ignores a repeat and closes', () => {
  const eggs = eggsOf().stencil.EasterEggs;
  for (const name of STAGED) {
    eggs[`${name}Mode`] = true;
    const live = currentLogoStage();
    assert.equal(live?.name, name);
    assert.equal(eggs[`${name}Mode`], true);
    const said = toasts.length;
    eggs[`${name}Mode`] = true;
    assert.equal(currentLogoStage(), live, `${name}: already on, not restarted`);
    assert.equal(toasts.length, said, 'and not announced again');
    eggs[`${name}Mode`] = false;
    assert.equal(currentLogoStage(), null);
    eggs[`${name}Mode`] = false;
    assert.equal(eggs[`${name}Mode`], false);
  }
});

test('a new show replaces the one up, and calling the one up changes nothing', () => {
  const eggs = eggsOf().stencil.EasterEggs;
  eggs.firework();
  const fire = currentLogoStage();
  eggs.firework();
  assert.equal(currentLogoStage(), fire, 'the same call does not restart it');
  eggs.waterShow();
  assert.equal(currentLogoStage().name, 'waterShow');
  assert.equal(eggs.fireworkMode, false, 'shows are mutually exclusive');
  eggs.runawayMode = true;
  assert.equal(currentLogoStage().name, 'runaway');
  assert.equal(toasts.length, 3);
  closeLogoStage();
});

test('a show closes an open window, a leftover overlay and fullscreen before it opens', () => {
  let full = true, shellOpen = true;
  doc.body.classList.contains = (c) => c === 'fullscreen-mode' && full;
  const shell = { stacked: false, isOpen: () => shellOpen, close: () => { shellOpen = false; }, takesEscape: () => true };
  const overlay = createStubElement('div');
  overlay.classList.add('modal-open');
  const closeBtn = createStubElement('button', { click: () => overlay.classList.remove('modal-open') });
  overlay.querySelector = (sel) => (sel === '.app-modal-close' ? closeBtn : null);
  overlays.push(overlay);
  modalShells.add(shell);
  try {
    const { stencil } = eggsOf({ toggleFullscreen: () => { full = !full; } });
    stencil.EasterEggs.neonOn();
    assert.equal(shellOpen, false, 'the window closed');
    assert.equal(overlay.classList.contains('modal-open'), false, 'the confirm-style overlay too');
    assert.equal(full, false, 'fullscreen is left');
    assert.equal(currentLogoStage()?.name, 'neonOn');
  } finally {
    modalShells.delete(shell);
    closeLogoStage();
  }
});

test('webcoreMode = true dresses the page only; webcore() still makes the picture', async () => {
  const { app, stencil } = eggsOf({ loadImageFromFile() { app.calls.push(['load']); } });
  stencil.EasterEggs.neonOn();
  stencil.EasterEggs.webcoreMode = true;
  await Promise.resolve();
  assert.equal(webcoreActive(), true);
  assert.equal(stencil.EasterEggs.webcoreMode, true);
  assert.equal(app.image, null, 'no picture');
  assert.equal(app.calls.some(([k]) => k === 'load' || k === 'createBlankImage'), false, 'no project either');
  assert.equal(currentLogoStage()?.name, 'neonOn', 'the skin never closes a show');
  stencil.EasterEggs.makeItSmall();
  assert.equal(currentLogoStage()?.name, 'makeItSmall', 'and a show opens under the skin');
  stencil.EasterEggs.webcoreMode = true;
  stencil.EasterEggs.webcoreMode = false;
  await Promise.resolve();
  assert.equal(webcoreActive(), false);
  closeLogoStage();
});

test('pinkVibeMode is the pink tint: on once, off back to no filter', async () => {
  const { app, stencil } = eggsOf({
    image: { width: 200, height: 100 },
    setImageFilter(v) { app.imageFilter = v; },
    setFilterColor(v) { app.filterColor = v; },
    installLayout() { app.calls.push(['layout']); return true; },
  });
  const eggs = stencil.EasterEggs;
  eggs.pinkVibeMode = true;
  await Promise.resolve();
  assert.equal(app.imageFilter, 'custom');
  assert.equal(app.filterColor, STAGE.pink.tint);
  assert.equal(eggs.pinkVibeMode, true);
  eggs.pinkVibeMode = true;
  eggs.pinkVibe();
  await Promise.resolve();
  assert.equal(app.calls.filter(([k]) => k === 'layout').length, 1, 'one heart, however often asked');
  eggs.pinkVibeMode = false;
  assert.equal(app.imageFilter, 'none');
  assert.equal(eggs.pinkVibeMode, false);
});

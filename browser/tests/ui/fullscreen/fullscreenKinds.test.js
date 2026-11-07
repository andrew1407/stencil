// The two fullscreens (js/ui/fullscreen/button.js, layer.js, screen.js): click → the OS full
// screen, double-click → the in-window layer, right-click or a held press → a menu of both; the
// canvas menu carries the same pair as a flyout; Alt+F and Alt+Shift+F reach each kind.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { installDom } from '../../helpers/dom.js';
import { createFsButtonGesture, fsRowFace, fsButtonTitle, FS_KINDS } from '../../../js/ui/fullscreen/button.js';
import { contextMenuInner } from '../../../js/ui/contextMenu/markup.js';

const fakeTimers = () => {
  const due = new Map();
  let n = 0;
  return {
    setTimer: (fn, ms) => { due.set(++n, { fn, ms }); return n; },
    clearTimer: (id) => due.delete(id),
    fire: () => { const all = [...due.values()]; due.clear(); all.forEach((t) => t.fn()); },
    pending: () => due.size,
  };
};
const rig = () => {
  const t = fakeTimers();
  const log = [];
  const g = createFsButtonGesture({
    single: () => log.push('single'), double: () => log.push('double'), openMenu: (held) => log.push(held ? 'menu:held' : 'menu'),
    setTimer: t.setTimer, clearTimer: t.clearTimer,
  });
  return { g, t, log };
};

test('one click waits out the double-click interval, then means the OS full screen', () => {
  const { g, t, log } = rig();
  g.click();
  assert.deepEqual(log, [], 'nothing before the interval');
  t.fire();
  assert.deepEqual(log, ['single']);
});

test('two clicks inside the interval are the in-window kind, and never the single', () => {
  const { g, t, log } = rig();
  g.click();
  g.click();
  t.fire();
  assert.deepEqual(log, ['double'], 'counted here, so the strip copy relaying plain clicks still doubles');
});

test('right-click opens the menu and swallows the click a long press may trail', () => {
  const { g, t, log } = rig();
  g.contextmenu();
  g.click();
  t.fire();
  assert.deepEqual(log, ['menu']);
});

test('a held left press opens the menu; moving past the slop or a right button does not', () => {
  const held = rig();
  held.g.pressStart({ x: 5, y: 5, button: 0 });
  held.t.fire();
  held.g.pressEnd();
  held.g.click();
  held.t.fire();
  assert.deepEqual(held.log, ['menu:held'], 'opened as held, so its release picks a row; it is not a click');

  const moved = rig();
  moved.g.pressStart({ x: 0, y: 0, button: 0 });
  moved.g.pressMove({ x: 40, y: 0 });
  moved.t.fire();
  assert.deepEqual(moved.log, [], 'a drag is not a hold');

  const right = rig();
  right.g.pressStart({ x: 0, y: 0, button: 2 });
  assert.equal(right.t.pending(), 0, 'only the primary button arms the hold');
});

test('each kind has its own face and hotkey; the live kind\'s row is its exit', () => {
  assert.deepEqual(FS_KINDS, ['screen', 'window']);
  assert.deepEqual(fsRowFace('screen', null), { label: 'Full Screen', icon: 'monitor', hk: 'fullscreen' });
  assert.deepEqual(fsRowFace('window', null), { label: 'Fullscreen in Window', icon: 'maximize', hk: 'fullscreenWindow' });
  assert.equal(fsRowFace('window', 'window').label, 'Exit Fullscreen in Window');
  assert.equal(fsRowFace('window', 'window').icon, 'minimize');
  assert.equal(fsRowFace('screen', 'window').label, 'Full Screen', 'the other kind stays a switch');
});

test('the tooltip names the double-click and its Alt+Shift+F, and the menu gestures', () => {
  const combo = (id) => ({ fullscreenWindow: 'Alt+Shift+F' })[id] || '';
  const off = fsButtonTitle(null, combo);
  assert.match(off, /^Full screen\n/);
  assert.match(off, /Double-click — in the window, Alt\+Shift\+F/);
  assert.match(off, /Right-click or hold — both kinds$/);
  assert.doesNotMatch(fsButtonTitle('screen', combo), /Double-click/, 'while on, a click only leaves');
});

test('the canvas menu\'s Fullscreen row is a flyout of both kinds, each with its hotkey chip', () => {
  const m = contextMenuInner();
  const item = m.slice(m.indexOf('id="ctx-fullscreen"'), m.indexOf('id="ctx-script"'));
  assert.match(item, /id="ctx-fullscreen-sub"/);
  assert.match(item, /id="ctx-fs-screen" data-fs-kind="screen"[\s\S]*data-hk="fullscreen"/);
  assert.match(item, /id="ctx-fs-window" data-fs-kind="window"[\s\S]*data-hk="fullscreenWindow"/);
});

test('Alt+Shift+F is in-window fullscreen; Flip Horizontally moved to Alt+Shift+H', () => {
  const table = JSON.parse(readFileSync(new URL('../../../../common/config/hotkeysConfig.json', import.meta.url)));
  const by = Object.fromEntries(table.map((h) => [h.id, h.default]));
  assert.equal(by.fullscreen, 'Alt+F');
  assert.equal(by.fullscreenWindow, 'Alt+Shift+F');
  assert.equal(by.flipImageHorizontal, 'Alt+Shift+H');
});

test('the layer switches between kinds without leaving, and falls back in-window when the page may not', async () => {
  const doc = installDom({}, {
    window: { innerWidth: 1000, innerHeight: 700, addEventListener() {} },
    requestAnimationFrame: () => 1,
  });
  for (const id of ['fs-controls-panel', 'fs-points-panel', 'fs-top-trigger', 'fs-right-trigger',
    'fullscreen-toggle', 'canvas-viewport']) doc.register(id, doc.createElement('div'));
  try {
    const { StencilFullscreenLayer } = await import('../../../js/ui/fullscreen/layer.js');
    const app = { image: null, zoomPan: { syncViewportHeight() {} } };
    new StencilFullscreenLayer().wire(app);
    app.toggleFullscreen('window');
    assert.equal(app.fullscreenKind(), 'window');
    app.toggleFullscreen('screen');
    assert.equal(app.fullscreenKind(), 'screen', 'a switch, not an exit');
    await new Promise((r) => setImmediate(r));
    assert.equal(app.fullscreenKind(), 'window', 'no Fullscreen API here: the layer alone stays');
    app.toggleFullscreen('window');
    assert.equal(app.fullscreenKind(), null, 'the live kind again leaves');
    app.toggleFullscreen('window');
    app.toggleFullscreen();
    assert.equal(app.fullscreenKind(), null, 'no kind while on always leaves (the logo stage, the API)');
  } finally {
    doc.restore();
  }
});

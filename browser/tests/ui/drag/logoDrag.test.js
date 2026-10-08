// The logo drag (js/ui/drag/logoDrag.js): over the canvas the renderer previews the clean view and
// nothing the app holds moves; dropped there, the clean view commits through each toolbar setter
// (one filter step, and only what is applied changes); dropped anywhere else, released back over
// the logo or cancelled, nothing changes. A modified press, one whose hold fired, or one while the
// accent menu is up, is no drag.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { SettingsController, makeApp } from '../../helpers/settingsControllerRig.js';
import { createStubElement } from '../../helpers/dom.js';

const { commitCleanView, logoDragHooks } = await import('../../../js/ui/drag/logoDrag.js');
const { createIconDrag, TARGET_CLASS, TARGET_OVER_CLASS } = await import('../../../js/ui/drag/iconDrag.js');

// A partial app over the real SettingsController; its renderer records the previews it is asked for.
const rig = (over = {}) => {
  const app = makeApp({
    image: { width: 6, height: 4 }, imageFilter: 'sepia', compareMode: 'vertical',
    history: { historyStep: -1, history: [], floor: { filter: 'sepia', filterColor: '#7c3aed' } }, ...over,
  });
  app.steps = 0;
  app.saveHistory = () => { app.steps++; };
  app.settings = new SettingsController(app);
  app.previews = [];
  app.renderer.previewClean = (on) => app.previews.push(on);
  const canvas = createStubElement('canvas');
  const viewport = createStubElement('div', { contains: (n) => n === canvas });
  const hold = { cancelled: 0, fired: false, cancel() { this.cancelled++; } };
  const hooks = logoDragHooks(app, { viewport: () => viewport, hold });
  return { app, hooks, viewport, canvas, hold, elsewhere: createStubElement('div') };
};

const SETTINGS = ['imageFilter', 'showLines', 'showPoints', 'compareMode'];
const settingsOf = (app) => Object.fromEntries(SETTINGS.map((k) => [k, app[k]]));
const CLEAN = { imageFilter: 'none', showLines: false, showPoints: false, compareMode: 'none' };

test('the commit turns each applied one off through its setter, as one filter step', () => {
  const { app } = rig();
  commitCleanView(app);
  assert.deepEqual(settingsOf(app), CLEAN);
  assert.equal(app.steps, 1, 'the filter moved off the step on screen: one undo step');
  assert.ok(app.rec.save > 0, 'the project saves, as a toolbar change does');
  assert.equal(app.rec.remoteSync, 1, 'the filter syncs; the display toggles never do');
});

test('only what is applied changes: a clean view already records and saves nothing', () => {
  const { app } = rig({ imageFilter: 'none', showLines: false, showPoints: false, compareMode: 'none' });
  commitCleanView(app);
  assert.deepEqual([app.steps, app.rec.save, app.rec.redraw], [0, 0, 0]);
  const partly = rig({ imageFilter: 'none', showPoints: false, compareMode: 'none' }).app;
  commitCleanView(partly);
  assert.deepEqual(settingsOf(partly), CLEAN);
  assert.equal(partly.steps, 0, 'no filter step when the picture was already unfiltered');
});

test('a modified press, one whose hold opened a show, or one while the menu is up, is never a drag', () => {
  for (const mod of ['altKey', 'ctrlKey', 'metaKey', 'shiftKey']) {
    const { hooks, hold } = rig();
    assert.equal(hooks.start({ event: { [mod]: true } }), false, mod);
    assert.equal(hold.cancelled, 0);
  }
  const { hooks, hold } = rig();
  hold.fired = true;
  assert.equal(hooks.start({ event: {} }), false, 'the press is the show\'s');
  const { app, viewport } = rig();
  const menuHooks = logoDragHooks(app, { viewport: () => viewport, menuUp: () => true });
  assert.equal(menuHooks.start({ event: {} }), false, 'the accent menu up owns the press');
});

test('the start drops the waiting hold and offers the canvas as the target', () => {
  const { hooks, hold, viewport } = rig();
  assert.notEqual(hooks.start({ event: {} }), false);
  assert.equal(hold.cancelled, 1, 'no show starts mid-drag');
  assert.ok(viewport.classes.has(TARGET_CLASS));
  assert.ok(!viewport.classes.has(TARGET_OVER_CLASS));
});

test('over the canvas the view previews clean and every setting stays; off it, the preview ends', () => {
  const { app, hooks, viewport, canvas, elsewhere } = rig();
  const before = settingsOf(app);
  hooks.start({ event: {} });
  hooks.move({ target: canvas });
  hooks.move({ target: canvas });
  assert.deepEqual(app.previews, [true], 'asked once, not per move');
  assert.ok(viewport.classes.has(TARGET_OVER_CLASS));
  assert.deepEqual(settingsOf(app), before);
  assert.deepEqual([app.steps, app.rec.save], [0, 0], 'no history, no storage');
  hooks.move({ target: elsewhere });
  assert.deepEqual(app.previews, [true, false]);
  assert.ok(!viewport.classes.has(TARGET_OVER_CLASS));
});

test('dropped on the canvas the clean view commits and the preview ends', () => {
  const { app, hooks, viewport, canvas } = rig();
  hooks.start({ event: {} });
  hooks.move({ target: canvas });
  hooks.drop({ target: canvas });
  assert.deepEqual(settingsOf(app), CLEAN);
  assert.deepEqual(app.previews, [true, false]);
  assert.ok(!viewport.classes.has(TARGET_CLASS), 'the target glow is gone');
});

test('dropped elsewhere, or cancelled, nothing changes and the preview is gone', () => {
  for (const end of ['drop', 'cancel']) {
    const { app, hooks, viewport, canvas, elsewhere } = rig();
    const before = settingsOf(app);
    hooks.start({ event: {} });
    hooks.move({ target: canvas });
    if (end === 'drop') hooks.drop({ target: elsewhere }); else hooks.cancel();
    assert.deepEqual(settingsOf(app), before, end);
    assert.deepEqual(app.previews, [true, false], end);
    assert.ok(!viewport.classes.has(TARGET_CLASS), end);
  }
});

test('with no picture the canvas is no target: no preview, and a drop there changes nothing', () => {
  const { app, hooks, viewport, canvas } = rig({ image: null });
  const before = settingsOf(app);
  hooks.start({ event: {} });
  assert.ok(!viewport.classes.has(TARGET_CLASS));
  hooks.move({ target: canvas });
  hooks.drop({ target: canvas });
  assert.deepEqual(app.previews, []);
  assert.deepEqual(settingsOf(app), before);
});

test('through the drag machine: released back over the logo, nothing happens', () => {
  const { app, hooks, canvas } = rig();
  const before = settingsOf(app);
  const logo = { left: 0, top: 0, right: 32, bottom: 32 };
  const at = (x, y) => (x > 100 ? canvas : null);
  const m = createIconDrag({ ...hooks, originRect: () => logo, targetAt: at });
  m.press(16, 16, {});
  m.move(400, 300);
  assert.deepEqual(app.previews, [true]);
  m.move(18, 14);
  m.release(18, 14);
  assert.deepEqual(settingsOf(app), before);
  assert.deepEqual(app.previews, [true, false]);
  m.press(16, 16, {});
  m.move(400, 300);
  m.release(400, 300);
  assert.deepEqual(settingsOf(app), CLEAN, 'the same gesture ending on the canvas commits');
});

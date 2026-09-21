// The hold on the header mark and the pink edit (js/ui/logoStageTrigger.js): the hold opens the
// accent's show without cycling the accent, and the pink show tints the page and draws its heart.
import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from './helpers/dom.js';

let doc, body, toasts;
const setup = () => {
  toasts = [];
  body = createStubElement('body');
  const balloon = createStubElement('stencil-notifications', {
    notify: (msg, type, opts) => toasts.push({ msg, type, opts }),
  });
  doc = installDom({ body, getElementById: (id) => (id === 'notify-balloon' ? balloon : null) });
  globalThis.window = globalThis.window || {};
  globalThis.innerWidth = 1000; globalThis.innerHeight = 600;
  globalThis.requestAnimationFrame = () => 1;
  globalThis.cancelAnimationFrame = () => {};
  globalThis.performance = { now: () => 0 };
};
beforeEach(setup);

const { wireLogoHold, heldShow, activateShow, pinkVibe } = await import('../js/ui/logo/logoStageTrigger.js');
const { logoStageOpen, closeLogoStage } = await import('../js/ui/logo/logoStage.js');
const { STAGE } = await import('../js/ui/logo/logoStageRules.js');

// A logo whose wrap is its own element, as .app-logo-wrap is in the topbar.
const makeLogo = () => {
  const wrap = createStubElement('span', { getBoundingClientRect: () => ({ left: 8, top: 8, width: 32, height: 32 }) });
  const logo = createStubElement('svg', { closest: () => wrap });
  return { logo, wrap };
};
const press = (wrap, ev = {}) => wrap.dispatch('pointerdown', { button: 0, clientX: 20, clientY: 20, ...ev });

test('the hold opens the accent\'s show, and the release cannot reach the accent cycle', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { logo, wrap } = makeLogo();
  const app = { accent: 'violet', customAccent: null };
  wireLogoHold(logo, app);
  press(wrap);
  t.mock.timers.tick(STAGE.holdMs);
  assert.equal(logoStageOpen(), true, 'held long enough: the show is up');
  assert.equal(toasts.length, 1);
  assert.equal(toasts[0].msg, STAGE.toast);
  assert.equal(toasts[0].opts.shine, true, 'the notice wears its lit border');
  // The click that follows the hold is taken before the logo's own handler sees it.
  let reached = false;
  const swallow = wrap.listeners.click?.[0];
  assert.ok(swallow, 'a one-shot click guard is armed');
  swallow({ preventDefault() {}, stopImmediatePropagation() { reached = true; } });
  assert.equal(reached, true, 'the click is stopped, so no accent cycle follows');
  closeLogoStage();
});

test('a short press, a drag and a modified press all open nothing', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { logo, wrap } = makeLogo();
  wireLogoHold(logo, { accent: 'violet', customAccent: null });

  press(wrap); wrap.dispatch('pointerup'); t.mock.timers.tick(STAGE.holdMs);
  assert.equal(logoStageOpen(), false, 'let go too soon');

  press(wrap); wrap.dispatch('pointermove', { clientX: 60, clientY: 20 }); t.mock.timers.tick(STAGE.holdMs);
  assert.equal(logoStageOpen(), false, 'dragged off the mark');

  press(wrap); wrap.dispatch('pointerleave'); t.mock.timers.tick(STAGE.holdMs);
  assert.equal(logoStageOpen(), false, 'pointer left');

  press(wrap, { altKey: true }); t.mock.timers.tick(STAGE.holdMs);
  assert.equal(logoStageOpen(), false, 'Alt belongs to the accent menu');
  assert.equal(toasts.length, 0);
});

test('an accent whose show wants another motion mode holds nothing', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { logo, wrap } = makeLogo();
  const app = { accent: 'crimson', customAccent: null };   // fire, but the mode is dust
  assert.equal(heldShow(app), null);
  wireLogoHold(logo, app);
  press(wrap);
  t.mock.timers.tick(STAGE.holdMs);
  assert.equal(logoStageOpen(), false);
  assert.equal(toasts.length, 0, 'nothing happened, so nothing is announced');
});

test('the pink show tints the page and adds the heart as one step', async () => {
  const calls = [];
  const app = {
    image: { width: 400, height: 300 },
    settings: {
      setImageFilter: (v) => calls.push(['filter', v]),
      setFilterColor: (v) => calls.push(['tint', v]),
    },
    export: { installLayout: (data, opts) => { calls.push(['layout', data, opts]); return true; } },
  };
  assert.equal(await pinkVibe(app), true);
  assert.deepEqual(calls[0], ['filter', 'custom']);
  assert.deepEqual(calls[1], ['tint', STAGE.pink.tint]);
  const [, data, opts] = calls[2];
  assert.deepEqual(opts, { mode: 'combine', history: true }, 'one undoable step, kept beside what is there');
  assert.equal(data.lines.length, 1);
  assert.equal(data.lines[0].locked, true);
  assert.equal(data.lines[0].fillColor, STAGE.pink.heartFill);
  assert.equal(data.imageWidth, 400);
});

test('the pink show makes itself a page when the editor is empty', async () => {
  const calls = [];
  const app = {
    image: null,
    createBlankImage(opts) { calls.push(['blank', opts.color]); app.image = { width: 200, height: 200 }; },
    settings: { setImageFilter: () => {}, setFilterColor: () => {} },
    export: { installLayout: () => true },
  };
  assert.equal(await pinkVibe(app), true);
  assert.deepEqual(calls[0], ['blank', STAGE.pink.blank]);
});

test('every activation is gated: nothing runs while a stage is already up', () => {
  const app = { accent: 'violet', customAccent: null };
  assert.equal(activateShow('neonOn', app), true);
  assert.equal(activateShow('makeSomeSunshine', app), false);
  assert.equal(activateShow('nosuch', app), false);
  assert.equal(toasts.length, 1, 'only the one that ran spoke');
  closeLogoStage();
});

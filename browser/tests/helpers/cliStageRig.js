// The CLI stage under the DOM stub, for tests/ui/cli/stage*.test.js: the document with a live
// window bus, a stage over a stub app, and the timer helpers that step its effects.
import { installDom, createStubElement } from './dom.js';
import { installMemoryStorage } from './memoryStorage.js';
import FX from '../../../common/config/console/fx.json' with { type: 'json' };

installMemoryStorage();
export let doc, body;
const descendants = (el) => (el.children ?? []).flatMap((c) => [c, ...descendants(c)]);
const matches = (el, sel) => sel.split(',').some((s) => el.classList?.contains(s.trim().slice(1)));
export const installCliDom = () => {
  body = createStubElement('body', { querySelectorAll: () => [] });
  const balloon = createStubElement('stencil-notifications', { notify() {} });
  doc = installDom({
    body, getElementById: (id) => (id === 'notify-balloon' ? balloon : null), querySelectorAll: () => [],
    createElement: (tag) => {
      const el = createStubElement(tag);
      el.querySelectorAll = (sel) => descendants(el).filter((d) => matches(d, sel));
      return el;
    },
  }, { innerWidth: 1000, innerHeight: 600, requestAnimationFrame: () => 1, cancelAnimationFrame: () => {}, performance: { now: () => 0 } });
  doc.querySelector = () => null;
  // The bus publishes on the window, so the window is a live event target here.
  const bus = new EventTarget();
  globalThis.window = globalThis.window || {};
  globalThis.window.addEventListener = (...a) => bus.addEventListener(...a);
  globalThis.window.removeEventListener = (...a) => bus.removeEventListener(...a);
  globalThis.window.dispatchEvent = (e) => bus.dispatchEvent(e);
  return { doc, body };
};

const { makeApp, createStencil } = await import('./stencilApiRig.js');
const { openCliStage } = await import('../../js/ui/cli/stage.js');

export const rig = () => {
  const timers = [];
  const schedule = (fn, ms) => { timers.push([fn, ms]); return timers.length; };
  const app = makeApp({ image: { width: 4, height: 2 }, chat: { close() {}, open() {}, isOpen: () => false } });
  app.changes = { on: () => () => {} };
  window.stencil = createStencil(app);
  const stage = openCliStage(app, { doc, schedule });
  const banner = stage.host.children[0].children[0];
  const letters = () => [...banner.querySelectorAll('.cli-letter')];
  return { app, stage, timers, banner, letters };
};
export const presses = (timers) => timers.filter(([, ms]) => ms === FX.press.ms).length;
// Runs every timer, the ones a step schedules included, until the stage is quiet.
export const drain = (timers) => { while (timers.length) timers.shift()[0](); };

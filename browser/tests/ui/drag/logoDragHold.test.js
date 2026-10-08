// The logo drag against the mark's real hold (js/ui/logo/stageTrigger.js): a drag that starts
// before the hold fires drops it, so no show starts mid-drag; a press whose hold already opened a
// show stays the show's and never becomes a drag.
import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';

beforeEach(() => {
  const balloon = createStubElement('stencil-notifications', { notify() {} });
  installDom({ body: createStubElement('body'), getElementById: (id) => (id === 'notify-balloon' ? balloon : null) });
  globalThis.window = globalThis.window || {};
  globalThis.innerWidth = 1000;
  globalThis.innerHeight = 600;
  globalThis.requestAnimationFrame = () => 1;
  globalThis.cancelAnimationFrame = () => {};
  globalThis.performance = { now: () => 0 };
});

const { wireLogoHold } = await import('../../../js/ui/logo/stageTrigger.js');
const { logoStageOpen, closeLogoStage } = await import('../../../js/ui/logo/stage.js');
const { STAGE } = await import('../../../js/ui/logo/stageRules.js');
const { logoDragHooks } = await import('../../../js/ui/drag/logoDrag.js');

const mark = () => {
  const wrap = createStubElement('span', { getBoundingClientRect: () => ({ left: 8, top: 8, width: 32, height: 32 }) });
  const logo = createStubElement('svg', { closest: () => wrap });
  const app = { accent: 'violet', customAccent: null, image: null, renderer: { previewClean() {} } };
  const hold = wireLogoHold(logo, app);
  const hooks = logoDragHooks(app, { viewport: () => null, hold });
  return { wrap, hold, hooks };
};
const press = (wrap) => wrap.dispatch('pointerdown', { button: 0, clientX: 20, clientY: 20 });

test('a drag that starts before the hold fires drops it: no show opens mid-drag', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { wrap, hold, hooks } = mark();
  press(wrap);
  assert.notEqual(hooks.start({ event: {} }), false, 'the drag starts');
  t.mock.timers.tick(STAGE.holdMs);
  assert.equal(logoStageOpen(), false);
  assert.equal(hold.fired, false);
});

test('a press whose hold opened a show is never a drag', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { wrap, hold, hooks } = mark();
  press(wrap);
  t.mock.timers.tick(STAGE.holdMs);
  assert.equal(logoStageOpen(), true);
  assert.equal(hold.fired, true);
  assert.equal(hooks.start({ event: {} }), false);
  closeLogoStage();
  press(wrap);
  assert.equal(hold.fired, false, 'the next press starts clean');
});

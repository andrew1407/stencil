// No tooltip while a control is dragged (js/ui/tip/tipHold.js): the hold drops the control tip that
// is up at once (no dust, its cloud swept), keeps a pending one from appearing and new hovers from arming one, and the canvas readout
// (tooltip.js) shows nothing either; once it ends, hovering shows tips again.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installTooltipPage } from '../../helpers/tooltipRig.js';
import { TIP_SHOW_DELAY_MS } from '../../../js/ui/motion.js';

const page = await installTooltipPage();
const { holdTips, tipsHeld, onTipsHeld } = await import('../../../js/ui/tip/tipHold.js');
const { StencilTooltip } = await import('../../../js/ui/tip/tooltip.js');

test('a hold runs each watcher once as it begins, and only while one is registered', () => {
  const seen = [];
  const off = onTipsHeld(() => seen.push('drop'));
  holdTips(true);
  holdTips(true);
  assert.equal(tipsHeld(), true);
  holdTips(false);
  assert.equal(tipsHeld(), false);
  holdTips(true);
  holdTips(false);
  off();
  holdTips(true);
  holdTips(false);
  assert.deepEqual(seen, ['drop', 'drop'], 'once per hold, none after unregistering');
});

test('the control tip that is up goes as the hold begins', () => {
  page.reset();
  page.over(page.control('Zoom in'));
  page.run(TIP_SHOW_DELAY_MS);
  assert.equal(page.visible(), true);
  holdTips(true);
  assert.equal(page.visible(), false);
  holdTips(false);
});

test('the hold drops the tip at once: no dust out, and the cloud a press already started is swept', () => {
  page.reset();
  page.over(page.control('Zoom in'));
  page.run(TIP_SHOW_DELAY_MS);
  holdTips(true);
  assert.equal(page.visible(), false);
  assert.equal(page.tip.classes.has('surface-leaving'), false, 'no dust out');
  assert.equal(page.tip.__dustHost, null);
  holdTips(false);
  page.over(page.control('Zoom out'));
  page.run(TIP_SHOW_DELAY_MS);
  page.fire('pointerdown', {});
  assert.ok(page.tip.__dustHost, 'a press plays the tip out as dust');
  holdTips(true);
  assert.equal(page.tip.__dustHost, null, 'the drag that press became sweeps it');
  assert.equal(page.tip.classes.has('surface-leaving'), false);
  holdTips(false);
});

test('a tip armed before the hold never appears, and no hover arms one while it lasts', () => {
  page.reset();
  page.over(page.control('Zoom out'));
  holdTips(true);
  page.run(TIP_SHOW_DELAY_MS);
  assert.equal(page.visible(), false, 'the pending one stood down');
  page.over(page.control('Fit to window'));
  assert.deepEqual(page.delays(), [], 'nothing armed mid-drag');
  holdTips(false);
  page.over(page.control('Rotate image left'));
  page.run(TIP_SHOW_DELAY_MS);
  assert.equal(page.visible(), true, 'after the drag a hover shows its tip again');
});

test('the canvas readout shows nothing while held', () => {
  const tip = Object.create(StencilTooltip.prototype);
  let hidden = 0;
  Object.assign(tip, { style: {}, hide: () => { hidden += 1; }, position() {}, dust() {} });
  holdTips(true);
  tip.reveal(100, 100);
  assert.equal(tip.style.display, undefined, 'never put on screen');
  assert.equal(hidden, 1);
  holdTips(false);
  tip.reveal(100, 100);
  assert.equal(tip.style.display, 'block');
});

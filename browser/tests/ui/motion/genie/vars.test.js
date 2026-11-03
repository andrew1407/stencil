// The Sliding mode's window flight vars (js/ui/motion/genie/vars.js): the side the opener lies beyond,
// the tilt that pinches the end facing it, the stretch along the flight, and the pop for an opener
// over the window itself. The extension port's cases.
import test from 'node:test';
import assert from 'node:assert';

import { genieSide, genieVars, applyGenieVars, clearGenieVars, GENIE_VAR_NAMES,
         GENIE_TILT_DEG, GENIE_PINCH, GENIE_STRETCH, GENIE_POP } from '../../../../js/ui/motion/genie/vars.js';

const BOX = { left: 100, top: 200, width: 400, height: 300 };   // x 100–500, y 200–500
const icon = (x, y) => ({ left: x - 12, top: y - 12, width: 24, height: 24 });

test('genieSide: the edge the point lies beyond, the widest gap winning; none over the box', () => {
  assert.strictEqual(genieSide(BOX, 300, 50), 'up');
  assert.strictEqual(genieSide(BOX, 300, 700), 'down');
  assert.strictEqual(genieSide(BOX, 800, 300), 'right');
  assert.strictEqual(genieSide(BOX, 20, 530), 'left');
  assert.strictEqual(genieSide(BOX, 300, 300), null);
});

test('an opener above tilts the top edge away and stretches the box tall and narrow', () => {
  const v = genieVars(BOX, icon(300, 50));
  assert.strictEqual(v['--modal-rx'], `${GENIE_TILT_DEG}deg`);
  assert.strictEqual(v['--modal-ry'], '0deg');
  assert.strictEqual(v['--modal-kx'], String(GENIE_PINCH));
  assert.strictEqual(v['--modal-ky'], String(GENIE_STRETCH));
  assert.strictEqual(v['--modal-dx'], '0px');
  assert.strictEqual(v['--modal-dy'], '-300px');
  assert.strictEqual(v['--modal-sx'], String(24 / 400));
});

test('an opener to the left tilts the left edge away and stretches the box wide and low', () => {
  const v = genieVars(BOX, icon(20, 350));
  assert.strictEqual(v['--modal-rx'], '0deg');
  assert.strictEqual(v['--modal-ry'], `${-GENIE_TILT_DEG}deg`);
  assert.strictEqual(v['--modal-kx'], String(GENIE_STRETCH));
  assert.strictEqual(v['--modal-ky'], String(GENIE_PINCH));
});

test('an opener over the window pops it untilted; no opener falls from above', () => {
  const pop = genieVars(BOX, icon(300, 300));
  assert.deepStrictEqual([pop['--modal-rx'], pop['--modal-ry'], pop['--modal-kx'], pop['--modal-ky']],
    ['0deg', '0deg', String(GENIE_POP.kx), String(GENIE_POP.ky)]);
  const above = genieVars(BOX, null);
  assert.strictEqual(above['--modal-rx'], `${GENIE_TILT_DEG}deg`);
  assert.strictEqual(above['--modal-sx'], '0.05');
  assert.strictEqual(genieVars({ left: 0, top: 0, width: 0, height: 10 }, null), null);
});

test('applyGenieVars stamps every var and clearGenieVars removes them all', () => {
  const props = new Map();
  const el = { style: { setProperty: (k, v) => props.set(k, v), removeProperty: (k) => props.delete(k) } };
  assert.strictEqual(applyGenieVars(el, BOX, icon(300, 50)), true);
  assert.deepStrictEqual([...props.keys()].sort(), [...GENIE_VAR_NAMES].sort());
  clearGenieVars(el);
  assert.strictEqual(props.size, 0);
  assert.strictEqual(applyGenieVars(el, null, null), false);
});

// The modal popover's TOUCH gestures (js/ui/popover.js): a tap opens the full modal, a long
// press the popover, travel past the slop turns it into a scroll.
import { test } from 'node:test';
import assert from 'node:assert';
import { LONG_PRESS_MS, PRESS_SLOP_PX } from '../../../js/ui/tip/popover.js';
import { machine } from '../../helpers/popoverGestureRig.js';

// ── Touch ──
test('touch: a plain tap opens the FULL modal immediately — no double-tap wait', () => {
  const { timers, calls, g } = machine();
  g.pressStart({ x: 10, y: 10, touch: true });
  g.pressEnd();
  g.click();
  assert.deepStrictEqual(calls, ['full']);
  assert.strictEqual(timers.pending, 0, 'no deferred timer on touch');
});

test('touch: holding for the long-press interval opens the POPOVER, and the synthetic click is swallowed', () => {
  const { timers, calls, g } = machine();
  g.pressStart({ x: 10, y: 10, touch: true });
  timers.advance(LONG_PRESS_MS);
  assert.deepStrictEqual(calls, ['popover']);
  g.pressEnd();
  g.click();                       // the click browsers synthesize after the release
  assert.deepStrictEqual(calls, ['popover'], 'the follow-up click opened nothing');
});

test('touch: travelling past the slop turns the hold into a scroll — no popover', () => {
  const { timers, calls, g } = machine();
  g.pressStart({ x: 10, y: 10, touch: true });
  g.pressMove({ x: 10, y: 10 + PRESS_SLOP_PX + 1 });
  timers.advance(LONG_PRESS_MS);
  assert.deepStrictEqual(calls, []);
});

test('touch: releasing before the hold interval cancels the popover (it was a tap)', () => {
  const { timers, calls, g } = machine();
  g.pressStart({ x: 10, y: 10, touch: true });
  g.pressEnd();
  timers.advance(LONG_PRESS_MS);
  assert.deepStrictEqual(calls, [], 'the hold never fired');
  g.click();
  assert.deepStrictEqual(calls, ['full'], 'the tap still opens the full modal');
});

test('android: the synthetic contextmenu after a long press does not double-open, and swallows the click', () => {
  const { calls, g } = machine();
  g.pressStart({ x: 10, y: 10, touch: true });
  g.contextmenu();                 // Android fires this for the long press
  g.pressEnd();
  g.click();
  // One popover intent from contextmenu; the shell's openPopover is idempotent while
  // open, so even a hold-fired duplicate collapses to one visible popover.
  assert.deepStrictEqual(calls, ['popover']);
});

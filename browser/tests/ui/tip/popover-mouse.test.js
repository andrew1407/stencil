// The modal popover's MOUSE gestures (js/ui/tip/popover.js): a single click defers to the full
// modal, a double click takes the popover, a right-click cancels the pending click.
import { test } from 'node:test';
import assert from 'node:assert';
import { DOUBLE_CLICK_MS } from '../../../js/ui/tip/popover.js';
import { machine } from '../../helpers/popoverGestureRig.js';

test('mouse: a single click opens the FULL modal, after one double-click interval', () => {
  const { timers, calls, g } = machine();
  g.pressStart({ x: 0, y: 0, touch: false });
  g.pressEnd();
  g.click();
  assert.deepStrictEqual(calls, [], 'nothing yet — the click is deferred');
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(calls, ['full']);
});

test('mouse: a double click opens the POPOVER and the full modal never flashes', () => {
  const { timers, calls, g } = machine();
  g.pressStart({ touch: false }); g.pressEnd(); g.click();
  g.pressStart({ touch: false }); g.pressEnd(); g.click();
  g.dblclick();
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(calls, ['popover'], 'both deferred clicks were cancelled');
});

test('mouse: right-click opens the popover and cancels any pending click', () => {
  const { timers, calls, g } = machine();
  g.pressStart({ touch: false }); g.pressEnd(); g.click();
  g.contextmenu();
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(calls, ['popover']);
});

// The icon dragged off the toolbar (js/ui/drag/iconDrag.js) opens its window where it lands.
test('mouse: a drag that takes the press drops a click still waiting to open', () => {
  const { timers, calls, g } = machine();
  g.pressStart({ touch: false }); g.pressEnd(); g.click();
  g.pressStart({ touch: false });
  g.dragged();
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(calls, [], 'the earlier click cannot toggle what the drop opens');
  g.pressEnd(); g.click();
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(calls, ['full'], 'the next click is a click again');
});

// An Alt-peek machine whose open/close state is tracked like the real shell's: `engaged` mimes the owner's
// pointer-inside check at release time, `holdLinger` its mid-typing check.

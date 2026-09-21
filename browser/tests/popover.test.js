// The modal popover (js/ui/popover.js): the eager-click machine, the option forwarding and the
// placement rules. Pure and injected, so the whole matrix runs without a DOM.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { popoverPosition, DOUBLE_CLICK_MS } from '../js/ui/tip/popover.js';
import { machine, eagerMachine } from './helpers/popoverGestureRig.js';

test('eagerClick opens on the click itself, with no timer left pending', () => {
  const { timers, calls, g } = eagerMachine();
  g.click();
  assert.deepStrictEqual(calls, ['full'], 'open now, not in 250ms');
  assert.strictEqual(timers.pending, 0, 'no deferred open left behind');
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(calls, ['full'], 'and nothing fires later');
});

test('eagerClick still gets its popover from a double-click', () => {
  const { timers, calls, g } = eagerMachine();
  g.click();
  g.dblclick();
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(calls, ['full', 'popover'],
    'the second click re-shapes what the first opened');
});

// The wiring destructures its options explicitly, so a new one is silently dropped
// unless it is listed there too — the bug that left the chat still waiting 250ms.
test('wireModalOpenGestures forwards every option the machine understands', () => {
  const src = readFileSync(new URL('../js/ui/tip/popover.js', import.meta.url), 'utf8');
  const machineOpts = src.slice(src.indexOf('export const createModalOpenGesture = ({'),
                                src.indexOf('} = {}) => {'));
  const wiring = src.slice(src.indexOf('export const wireModalOpenGestures = (btn, {'));
  const forwarded = wiring.slice(0, wiring.indexOf('}) => {'));
  for (const name of ['openFull', 'openPopover', 'closePopover', 'isPopoverOpen',
                      'isPeekEngaged', 'holdLinger', 'eagerClick']) {
    assert.ok(machineOpts.includes(name), `${name} is a machine option`);
    assert.ok(forwarded.includes(name), `${name} must be forwarded by the wiring`);
  }
});

test('the default machine still defers, so a modal cannot flash open and shut', () => {
  const { timers, calls, g } = machine();
  g.click();
  assert.deepStrictEqual(calls, [], 'nothing yet');
  g.dblclick();
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(calls, ['popover'], 'the deferred open was cancelled');
});

// ── Placement ──
test('popoverPosition: below the anchor, left-aligned, with the gap', () => {
  const p = popoverPosition({
    anchor: { left: 100, top: 20, bottom: 44 },
    box: { width: 420, height: 300 },
    viewport: { width: 1280, height: 800 },
  });
  assert.deepStrictEqual(p, { left: 100, top: 52 });
});

test('popoverPosition: flips above when the bottom would overflow', () => {
  const p = popoverPosition({
    anchor: { left: 100, top: 700, bottom: 724 },
    box: { width: 420, height: 300 },
    viewport: { width: 1280, height: 800 },
  });
  assert.strictEqual(p.top, 700 - 8 - 300);
});

// A short box low in a modal still "fits" below by the whole viewport's measure, so flipping it up is what
// actually keeps it off the rows under it (user report).
test('popoverPosition: flips above when above has more room, even without overflowing below', () => {
  const p = popoverPosition({
    anchor: { left: 100, top: 640, bottom: 674 },
    box: { width: 190, height: 180 },
    viewport: { width: 1280, height: 900 },
  });
  assert.strictEqual(p.top, 640 - 8 - 180);
});

test('popoverPosition: clamps into the viewport when neither side fits', () => {
  // A box taller than the space above AND below pins to the bottom margin…
  const tall = popoverPosition({
    anchor: { left: 100, top: 300, bottom: 324 },
    box: { width: 420, height: 700 },
    viewport: { width: 1280, height: 760 },
  });
  assert.strictEqual(tall.top, 760 - 8 - 700);
  // …and never above the top margin.
  const huge = popoverPosition({
    anchor: { left: 100, top: 300, bottom: 324 },
    box: { width: 420, height: 900 },
    viewport: { width: 1280, height: 760 },
  });
  assert.strictEqual(huge.top, 8);
});

test('popoverPosition: clamps horizontally at both edges', () => {
  const right = popoverPosition({
    anchor: { left: 1200, top: 20, bottom: 44 },
    box: { width: 420, height: 300 },
    viewport: { width: 1280, height: 800 },
  });
  assert.strictEqual(right.left, 1280 - 8 - 420);
  const left = popoverPosition({
    anchor: { left: 2, top: 20, bottom: 44 },
    box: { width: 420, height: 300 },
    viewport: { width: 1280, height: 800 },
  });
  assert.strictEqual(left.left, 8);
});


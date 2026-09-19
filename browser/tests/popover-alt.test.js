// Alt+hover is hold-to-peek (js/ui/popover.js): it opens on hover, closes on release, never
// adopts a deliberate open, and an engaged peek lingers until the pointer leaves.
import { test } from 'node:test';
import assert from 'node:assert';
import { createModalOpenGesture, DOUBLE_CLICK_MS, LINGER_CLOSE_MS } from '../js/ui/popover.js';
import { stubTimers } from './helpers/popoverGestureRig.js';

const altMachine = () => {
  const timers = stubTimers();
  const calls = [];
  let open = false;
  const state = { engaged: false, holdLinger: false };
  const g = createModalOpenGesture({
    openFull: () => calls.push('full'),
    openPopover: () => { open = true; calls.push('popover'); },
    closePopover: () => { open = false; calls.push('close'); },
    isPopoverOpen: () => open,
    isPeekEngaged: () => state.engaged,
    holdLinger: () => state.holdLinger,
    setTimer: timers.setTimer,
    clearTimer: timers.clearTimer,
  });
  return { timers, calls, g, state, isOpen: () => open };
};

test('Alt+hover is HOLD-to-peek: opens on hover, closes on release, cancels a pending click', () => {
  const { timers, calls, g, isOpen } = altMachine();
  // A deferred click pending when the peek fires is cancelled, not queued behind it.
  g.pressStart({ touch: false }); g.pressEnd(); g.click();
  g.altHover();
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(calls, ['popover'], 'the peek opens immediately — no press involved');
  g.altRelease();
  assert.deepStrictEqual(calls, ['popover', 'close'], 'releasing Alt closes what the peek opened');
  assert.strictEqual(isOpen(), false);
  // …and a click AFTER the peek still works normally (nothing was swallowed).
  g.pressStart({ touch: false }); g.pressEnd(); g.click();
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(calls, ['popover', 'close', 'full']);
});

test('Alt release never closes a DELIBERATE open, and the peek never adopts one', () => {
  const { calls, g } = altMachine();
  g.dblclick();                    // deliberate → sticky
  g.altHover();                    // hovering the icon with Alt while it shows
  assert.deepStrictEqual(calls, ['popover'], 'the peek left the open popover alone');
  g.altRelease();
  assert.deepStrictEqual(calls, ['popover'], 'release closed nothing — it was not a peek');
  // A dblclick WHILE the Alt peek is showing converts it to sticky (the shell's
  // openPopover is idempotent while open, so its extra call shows nothing new).
  const m2 = altMachine();
  m2.g.altHover();
  m2.g.dblclick();
  m2.g.altRelease();
  assert.ok(!m2.calls.includes('close'), 'the dblclick claimed the peek — release keeps it');
  assert.strictEqual(m2.isOpen(), true);
});

test('altRelease with no peek showing is a safe no-op', () => {
  const { calls, g } = altMachine();
  g.altRelease();
  assert.deepStrictEqual(calls, []);
});

test('an ENGAGED peek (pointer inside at release) survives the Alt release and LINGERS', () => {
  const hover = altMachine();
  hover.g.altHover();
  hover.state.engaged = true;    // the pointer rests inside the box
  hover.g.altRelease();
  assert.deepStrictEqual(hover.calls, ['popover'], 'engaged — release keeps it');
  assert.strictEqual(hover.isOpen(), true);
  // Once lingering, a LATER Alt press+release still leaves it alone.
  hover.g.altRelease();
  assert.strictEqual(hover.isOpen(), true);
  // A fresh altHover on the SAME open window never adopts it.
  hover.calls.length = 0;
  hover.g.altHover();
  assert.deepStrictEqual(hover.calls, [], 'the peek never adopts the open window');
  // NOT engaged (pointer already outside at release, nothing typed) → closes,
  // even if the user had clicked something inside earlier.
  const away = altMachine();
  away.g.altHover();
  away.g.altRelease();
  assert.deepStrictEqual(away.calls, ['popover', 'close']);
});

test('a LINGERING window closes once the pointer leaves the box — unless mid-typing', () => {
  const m = altMachine();
  m.g.altHover();
  m.state.engaged = true;
  m.g.altRelease();              // engaged → lingers
  m.g.boxLeave();
  m.timers.advance(LINGER_CLOSE_MS);
  assert.deepStrictEqual(m.calls, ['popover', 'close'], 'hover-out closes the lingering window');
  // Re-entering before the grace lands cancels the close.
  const back = altMachine();
  back.g.altHover();
  back.state.engaged = true;
  back.g.altRelease();
  back.g.boxLeave();
  back.g.boxEnter();
  back.timers.advance(LINGER_CLOSE_MS);
  assert.deepStrictEqual(back.calls, ['popover'], 're-entering keeps it');
  // Mid-typing (holdLinger) the hover-out is a no-op.
  const typing = altMachine();
  typing.g.altHover();
  typing.state.engaged = true;
  typing.g.altRelease();
  typing.state.holdLinger = true;
  typing.g.boxLeave();
  typing.timers.advance(LINGER_CLOSE_MS);
  assert.deepStrictEqual(typing.calls, ['popover'], 'typing holds the lingering window');
  // A STICKY open never hover-binds.
  const sticky = altMachine();
  sticky.g.dblclick();
  sticky.g.boxLeave();
  sticky.timers.advance(LINGER_CLOSE_MS);
  assert.strictEqual(sticky.isOpen(), true, 'a deliberate open ignores hover-out');
});

test('an Alt glide onto another icon closes the previous mini window — any popover shape', () => {
  // Peek A open → altHover on B closes A and opens B.
  const a = altMachine();
  const b = altMachine();
  a.g.altHover();
  assert.strictEqual(a.isOpen(), true);
  b.g.altHover();
  assert.strictEqual(a.isOpen(), false, 'the glide closed the previous peek');
  assert.strictEqual(b.isOpen(), true);
  b.g.altRelease();              // tidy the registry for the cases below
  // A STICKY (dblclick) window is closed by the glide too.
  const sticky = altMachine();
  const next = altMachine();
  sticky.g.dblclick();
  assert.strictEqual(sticky.isOpen(), true);
  next.g.altHover();
  assert.strictEqual(sticky.isOpen(), false, 'the glide closed the sticky window');
  assert.strictEqual(next.isOpen(), true);
  next.g.altRelease();
  // A FULL modal (plain click — no popover mode) is never glide-closed.
  const full = altMachine();
  const peek = altMachine();
  full.g.pressStart({ touch: false }); full.g.pressEnd(); full.g.click();
  full.timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(full.calls, ['full']);
  peek.g.altHover();
  assert.ok(!full.calls.includes('close'), 'a full modal is never touched by the glide');
  peek.g.altRelease();
});


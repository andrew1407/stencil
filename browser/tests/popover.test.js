import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

// The modal popover (browser/js/ui/popover.js): the placement rules and the
// click/dblclick/right-click/long-press machine behind every modal-opening toolbar
// icon. Both are pure/injected, so the whole matrix — mouse and touch — runs without
// a DOM, in the style of tests/projectOpenGesture.test.js.
import {
  popoverPosition, createModalOpenGesture, DOUBLE_CLICK_MS, LONG_PRESS_MS, PRESS_SLOP_PX,
  LINGER_CLOSE_MS,
} from '../js/ui/popover.js';

// A controllable clock: timers fire only when the test advances it.
const stubTimers = () => {
  const jobs = new Map();
  let seq = 0;
  return {
    setTimer: (fn, ms) => { jobs.set(++seq, { fn, ms }); return seq; },
    clearTimer: (id) => jobs.delete(id),
    get pending() { return jobs.size; },
    advance(ms) {
      for (const [id, job] of [...jobs]) {
        if (job.ms <= ms) { jobs.delete(id); job.fn(); }
      }
    },
  };
};

const machine = () => {
  const timers = stubTimers();
  const calls = [];
  const g = createModalOpenGesture({
    openFull: () => calls.push('full'),
    openPopover: () => calls.push('popover'),
    setTimer: timers.setTimer,
    clearTimer: timers.clearTimer,
  });
  return { timers, calls, g };
};

// An eager machine acts on the FIRST click: the wait exists so a full modal never
// flashes open and shut under a double-click, and a panel whose popover gesture only
// re-shapes the same window (the chat) has no such flash to prevent — it just felt slow.
const eagerMachine = () => {
  const timers = stubTimers();
  const calls = [];
  const g = createModalOpenGesture({
    openFull: () => calls.push('full'),
    openPopover: () => calls.push('popover'),
    eagerClick: true,
    setTimer: timers.setTimer,
    clearTimer: timers.clearTimer,
  });
  return { timers, calls, g };
};

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
  const src = readFileSync(new URL('../js/ui/popover.js', import.meta.url), 'utf8');
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

// ── Mouse ──
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

// An Alt-peek machine whose open/close state is tracked, like the real shell's.
// `engaged` mimics the owner's pointer-inside check at release time; `holdLinger`
// the owner's mid-typing check.
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

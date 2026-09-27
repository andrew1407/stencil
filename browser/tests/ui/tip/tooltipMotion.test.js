// The control tooltip's MOTION (js/ui/tip/controlTooltip.js + components.css). Pinned: it fades and rises
// through a TRANSITION, not keyframes, because one shared element is re-pointed many times a second on
// a toolbar sweep and a transition re-aims from wherever it is; it cannot be STRANDED when the control
// under the pointer vanishes, since a detached element never fires pointerout; and pressing the
// shortcut the visible tooltip shows shakes that keycap instead of dismissing the tip.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { TIP_SHOW_DELAY_MS } from '../../../js/ui/motion.js';
import { COMPONENTS_CSS } from '../../helpers/css.js';
import { installTooltipPage } from '../../helpers/tooltipRig.js';

const componentsCss = COMPONENTS_CSS;
const page = await installTooltipPage();

// ── 1. Appear / disappear ───────────────────────────────────────────────────

test('the tooltip forms from sand and disperses again, on a transition not an animation', () => {
  const block = componentsCss.match(/#app-tooltip \{([\s\S]*?)\n\}/);
  assert.ok(block, '#app-tooltip is styled');
  assert.match(block[1], /opacity: 0;/, 'hidden at rest');
  assert.match(block[1], /--dissolve: 1;/, '…and fully grained: it is sand before it is a box');
  assert.match(block[1], /transform: translate\(3px, 6px\) scale\(0\.96\);/,
    'offset toward the cursor, so the grain gathers out of it');
  assert.match(block[1], /transition: opacity \d+ms [^;]*,\s*\n?\s*transform \d+ms[^;]*,\s*\n?\s*--dissolve \d+ms/,
    'all three transition — the grain included');
  assert.ok(!/animation:/.test(block[1]),
    'no keyframes — a sweep across many controls would restart one mid-flight');
  const shown = componentsCss.match(/#app-tooltip\.visible \{([\s\S]*?)\n\}/);
  assert.ok(shown, '`.visible` is the whole of the shown state, so removing it plays the exit');
  assert.match(shown[1], /opacity: 1;[\s\S]*transform: none;[\s\S]*--dissolve: 0;/);
  // A transition is taken from the state it goes TO, so the base rule is the exit: readable fast, coming
  // apart slowly — the reverse would make a toolbar sweep wait on the grain.
  const ms = (css) => Number(css.match(/transition: opacity (\d+)ms/)[1]);
  assert.ok(ms(shown[1]) < ms(block[1]),
    'opaque sooner than it fades, so the grain is what you watch either way');
});

// The tooltip is re-pointed many times a second and tracks the cursor while up, so a mote layer measured
// at one position is stranded a frame later: it wears the sand as a MASK (animations.css .reveal-masked).
test('the tooltip’s sand is the app’s own grain: three coprime dot grids, unioned', () => {
  const block = componentsCss.match(/#app-tooltip \{([\s\S]*?)\n\}/)[1];
  for (const prop of ['-webkit-mask-image', 'mask-image']) {
    assert.ok(block.includes(`${prop}:`), `${prop} is set (both spellings ship)`);
  }
  const dots = block.match(/radial-gradient\(circle at 50% 50%/g) || [];
  assert.equal(dots.length, 6, 'three grids, in both mask spellings');
  assert.match(block, /mask-size: 4px 4px, 7px 7px, 11px 11px;/, 'coprime cells, like the row grain');
  assert.match(block, /mask-position: 0 0, 2px 3px, 5px 1px;/, 'and at three phases, so no lattice forms');
  assert.match(block, /mask-composite: add;/, 'UNION — an intersect would punch holes in a settled tip');
  // Solid when settled is not negotiable: a tooltip you cannot read is not decoration.
  // 120% of the half-diagonal is 0.85 of the cell, well past the 0.707 that covers it.
  const stops = block.match(/calc\(120% - var\(--dissolve\) \* (\d+)%\)/g) || [];
  assert.equal(stops.length, 6, 'every grid starts solid at --dissolve 0');
  // Each grid dies at a different rate, so they thin out in sequence, not together.
  const rates = [...block.matchAll(/#000 max\(0%, calc\(120% - var\(--dissolve\) \* (\d+)%\)\)/g)]
    .map((m) => Number(m[1]));
  assert.equal(new Set(rates).size, 3, 'three distinct death rates');
});

test('no reflow: the sand is a mask, so the tooltip’s box never changes', () => {
  const block = componentsCss.match(/#app-tooltip \{([\s\S]*?)\n\}/)[1];
  // Only opacity / transform / mask move. Anything that resizes the box would break
  // place(), which measures offsetWidth to clamp the tip against the screen edge.
  for (const prop of ['width:', 'height:', 'padding:', 'margin:', 'font-size:']) {
    const after = block.slice(block.indexOf('--dissolve: 1;'));
    assert.ok(!after.includes(prop), `${prop} is not part of the effect`);
  }
});

test('the tooltip grows away from the cursor it is anchored to', () => {
  const block = componentsCss.match(/#app-tooltip \{([\s\S]*?)\n\}/)[1];
  assert.match(block, /transform-origin: top left;/);
});

test('positioning measures the LAYOUT box, so the entry scale cannot mis-clamp it', () => {
  // A client rect mid-transition reports the scaled box; clamping a tooltip near the
  // screen edge against a box 3% too small settles it a few pixels off the edge.
  page.reset();
  page.page.reduced = true;
  const el = page.control('Save (Ctrl+S)');
  page.over(el, { clientX: 950, clientY: 790 });
  page.run(TIP_SHOW_DELAY_MS);
  assert.ok(page.visible());
  assert.equal(page.tip.style.left, `${1000 - 120 - 10}px`, 'clamped by the 120px layout width, not the 116px painted one');
  assert.equal(page.tip.style.top, `${790 - 40 - 12}px`, 'and flipped above by the 40px layout height');
  page.move({ clientX: 2, clientY: 3 });
  assert.deepEqual([page.tip.style.left, page.tip.style.top], ['16px', '21px'], 'it follows the cursor');
  page.move({ clientX: -50, clientY: -50 });
  assert.deepEqual([page.tip.style.left, page.tip.style.top], ['10px', '10px'], 'and the window clamps still hold');
});

test('the show delay is unchanged — the transition is layered over it, not instead of it', () => {
  assert.equal(TIP_SHOW_DELAY_MS, 200, 'the shared wake-up delay keeps its value');
  page.reset();
  const el = page.control('Save (Ctrl+S)');
  page.over(el);
  assert.deepEqual(page.delays(), [TIP_SHOW_DELAY_MS], 'hovering arms the show on the shared delay');
  assert.equal(page.visible(), false, 'nothing shows before it');
  page.run(TIP_SHOW_DELAY_MS);
  assert.equal(page.visible(), true);
  // Every dismissal still goes through the one hide(), which clears that timer first.
  const dismissals = [['scroll', () => page.fire('scroll', {})], ['pointerdown', () => page.fire('pointerdown', {})],
    ['keydown', () => page.key({ key: 'x', code: 'KeyX' })], ['blur', () => page.blur()]];
  for (const [name, dismiss] of dismissals) {
    page.reset();
    page.over(page.control('Open (Ctrl+O)'));
    dismiss();
    assert.equal(page.run(TIP_SHOW_DELAY_MS), 0, `${name} drops the pending show`);
    assert.equal(page.visible(), false, `${name}: no tooltip wakes up afterwards`);
  }
});

// ── 2. Never stranded ───────────────────────────────────────────────────────

const shown = (text) => {
  page.reset();
  const el = page.control(text);
  page.over(el);
  page.run(TIP_SHOW_DELAY_MS);
  assert.ok(page.visible(), `${text} is up`);
  return el;
};

test('a tooltip whose control was removed from the document is dropped', () => {
  // Both pointer handlers check it: a fast sweep can produce moves with no `over`
  // in between, and a detached element never fires `pointerout` at all.
  shown('Save (Ctrl+S)').isConnected = false;
  page.move({ clientX: 111, clientY: 111 });
  assert.equal(page.visible(), false, 'guarded on pointermove');
  shown('Save (Ctrl+S)').isConnected = false;
  page.over(page.control(''));
  assert.equal(page.visible(), false, 'and on pointerover');
  for (const [ev, dismiss] of [['scroll', () => page.fire('scroll', {})], ['pointerdown', () => page.fire('pointerdown', {})]]) {
    shown('Save (Ctrl+S)');
    dismiss();
    assert.equal(page.visible(), false, `${ev} still dismisses`);
  }
  shown('Save (Ctrl+S)');
  page.blur();
  assert.equal(page.visible(), false, 'and so does the window losing focus');
});

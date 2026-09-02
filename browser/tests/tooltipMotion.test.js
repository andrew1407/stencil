// The control tooltip's MOTION (js/ui/controlTooltip.js + components.css):
//
//   1. It appears and leaves on a short fade + rise instead of blinking. Deliberately a
//      TRANSITION, not keyframes — one shared element is re-pointed many times a second
//      when the pointer sweeps a toolbar, and a transition simply re-aims from wherever
//      it is: nothing to restart, stack, or leave half-played.
//   2. It cannot be STRANDED: the control it describes can vanish under the pointer
//      (a toggle rewrites its face, a row re-renders) and a detached element never fires
//      pointerout.
//   3. Pressing the shortcut the visible tooltip is showing shakes that keycap instead of
//      dismissing the tip. The matching is pure, so it is tested without a keyboard.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { parseCombo, eventCombo, comboMatchesEvent } from '../js/ui/controlTooltip.js';
import { TIP_SHOW_DELAY_MS } from '../js/ui/motion.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const componentsCss = read('../css/components.css');
const tooltipJs = read('../js/ui/controlTooltip.js');

// A keydown as the DOM reports it. `code` is the PHYSICAL key, which is the only side
// that still says "A" when a Mac turns Alt+A into "å".
const press = (key, { code = '', ctrl = false, alt = false, shift = false, meta = false } = {}) =>
  ({ key, code, ctrlKey: ctrl, altKey: alt, shiftKey: shift, metaKey: meta });

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
  // Arriving and leaving are timed apart on purpose: a transition is taken from the
  // state it goes TO, so the base rule is the exit. It must be READABLE fast and come
  // apart slowly — the reverse would make a toolbar sweep wait on the grain.
  const ms = (css) => Number(css.match(/transition: opacity (\d+)ms/)[1]);
  assert.ok(ms(shown[1]) < ms(block[1]),
    'opaque sooner than it fades, so the grain is what you watch either way');
});

// The tooltip is the ONE overlay that cannot carry a mote layer: it is re-pointed many
// times a second on a toolbar sweep and it tracks the cursor while it is up, so a layer
// measured at one position is stranded a frame later. It takes the same sand as a MASK
// on itself instead — the scroll dissolve's three coprime dot grids (animations.css
// .reveal-masked) — which rides the box, costs one composited layer, and can neither
// stack nor strand nor leak a timer.
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
  const place = tooltipJs.slice(tooltipJs.indexOf('const place ='), tooltipJs.indexOf('const hide ='));
  assert.match(place, /tip\.offsetWidth/);
  assert.match(place, /tip\.offsetHeight/);
  assert.ok(!/getBoundingClientRect/.test(place), 'never a transformed rect');
  // The clamps themselves are untouched.
  assert.match(place, /window\.innerWidth/);
  assert.match(place, /window\.innerHeight/);
});

test('the show delay is unchanged — the transition is layered over it, not instead of it', () => {
  assert.match(tooltipJs, /const SHOW_DELAY_MS = TIP_SHOW_DELAY_MS;/);
  assert.equal(TIP_SHOW_DELAY_MS, 200, 'the shared wake-up delay keeps its value');
  assert.match(tooltipJs, /showTimer = setTimeout\(\(\) => reveal\(el\), SHOW_DELAY_MS\);/);
  // Every dismissal still goes through the one hide(), which clears that timer first.
  assert.match(tooltipJs, /const hide = \(\) => \{\s*\n\s*clearTimeout\(showTimer\);/);
});

// ── 2. Never stranded ───────────────────────────────────────────────────────

test('a tooltip whose control was removed from the document is dropped', () => {
  // Both pointer handlers check it: a fast sweep can produce moves with no `over`
  // in between, and a detached element never fires `pointerout` at all.
  const guards = tooltipJs.match(/if \(curEl && curEl\.isConnected === false\)/g) || [];
  assert.equal(guards.length, 2, 'guarded on pointerover AND pointermove');
  for (const ev of ['scroll', 'pointerdown']) {
    assert.ok(tooltipJs.includes(`document.addEventListener('${ev}', hide, true);`),
      `${ev} still dismisses`);
  }
  assert.match(tooltipJs, /window\.addEventListener\('blur', hide\);/);
});

// ── 3. The keycap shake ─────────────────────────────────────────────────────

test('parseCombo reads both the written and the Apple-glyph forms', () => {
  const alt = parseCombo('Alt+R');
  assert.deepEqual([...alt.mods], ['Alt']);
  assert.equal(alt.key, 'R');
  const mac = parseCombo('⇧⌘S');
  assert.deepEqual([...mac.mods].sort(), ['Meta', 'Shift']);
  assert.equal(mac.key, 'S');
  assert.equal(parseCombo('Ctrl+Shift+Z').key, 'Z');
  assert.deepEqual([...parseCombo('Ctrl+Shift+Z').mods].sort(), ['Ctrl', 'Shift']);
  // Aliases collapse, so "Cmd"/"Meta"/"⌘" are one modifier and "Esc"/"Escape" one key.
  assert.deepEqual([...parseCombo('Cmd+K').mods], ['Meta']);
  assert.equal(parseCombo('Esc').key, 'ESCAPE');
  // Modifier-only combos leave no key behind, so nothing can ever match them.
  assert.equal(parseCombo('Alt+Shift').key, '');
  assert.equal(parseCombo('').key, '');
  // A gesture cap keeps its word instead, which no keystroke reports.
  assert.equal(parseCombo('Shift+click').key, 'CLICK');
});

test('eventCombo keeps the physical key as well as the typed one', () => {
  const e = eventCombo(press('å', { code: 'KeyA', alt: true }));
  assert.deepEqual([...e.mods], ['Alt']);
  assert.ok(e.keys.includes('A'), 'the Mac’s accented character does not lose the key');
  assert.deepEqual(eventCombo(press('0', { code: 'Digit0' })).keys, ['0', '0']);
});

test('a keystroke matches the cap that spells it — and only that one', () => {
  assert.ok(comboMatchesEvent('Alt+R', press('r', { code: 'KeyR', alt: true })));
  assert.ok(comboMatchesEvent('Alt+0', press('º', { code: 'Digit0', alt: true })), 'Mac Alt+0');
  assert.ok(comboMatchesEvent('⇧⌘S', press('S', { code: 'KeyS', shift: true, meta: true })));
  assert.ok(comboMatchesEvent('Delete', press('Delete', { code: 'Delete' })));
  // Wrong modifiers, extra modifiers and the bare key are all misses.
  assert.ok(!comboMatchesEvent('Alt+R', press('r', { code: 'KeyR' })), 'no modifier');
  assert.ok(!comboMatchesEvent('Alt+R', press('r', { code: 'KeyR', alt: true, shift: true })), 'one too many');
  assert.ok(!comboMatchesEvent('Alt+R', press('t', { code: 'KeyT', alt: true })), 'another key');
  // A gesture cap ("Alt+click") is not a keystroke, and a modifier-only cap never fires.
  assert.ok(!comboMatchesEvent('Alt+click', press('Alt', { code: 'AltLeft', alt: true })));
  assert.ok(!comboMatchesEvent('Shift', press('Shift', { code: 'ShiftLeft', shift: true })));
});

test('every cap nudges once the tooltip has LANDED, announcing the shortcut', () => {
  // The shake's job is to draw the eye to the shortcut while you are READING the tip,
  // so it fires on the show — not only when the key happens to be pressed.
  assert.match(tooltipJs, /t\.classList\.add\('visible'\);[\s\S]{0,600}?place\(lastEvent\);[\s\S]{0,600}?shakeKeys\(t\);/,
    'shaken on every reveal, once it is placed');
  // …but only once the motes have arrived: a nudge played while the tip is still
  // assembling is a movement nobody can see, which is the whole point of it.
  assert.match(tooltipJs,
    /if \(dusted\) shakeTimer = setTimeout\(\(\) => \{ shakeTimer = null; shakeKeys\(t\); \}, TIP_IN_MS\);\s*\n\s*else shakeKeys\(t\);/,
    'the shake waits out the gather, and fires at once when there was none');
  // …and a tip dismissed or re-pointed mid-flight never shakes the caps of a tip that
  // has already gone: both routes drop the pending nudge first.
  assert.match(tooltipJs, /clearTimeout\(shakeTimer\);\s*\n\s*showTimer = shakeTimer = null;/);
  assert.match(tooltipJs, /clearTimeout\(shakeTimer\);\s*\n\s*if \(dusted\)/);
  assert.match(tooltipJs,
    /const shakeKeys = \(t\) => \{\s*\n\s*t\.querySelectorAll\('\.tip-key'\)\.forEach\(cap => flashClass\(cap, SHAKE_CLASS, SHAKE_MS\)\);/);
  // A tip with no shortcut has no caps, so the query is empty and nothing happens —
  // no guard needed, and none that could get it wrong.
  assert.ok(!/shakeKeys[\s\S]{0,200}if \(/.test(tooltipJs.slice(tooltipJs.indexOf('const shakeKeys'))),
    'no special case for a shortcut-less tooltip');
});

test('the shake is wired to the tooltip’s own caps, one shot, and Escape still dismisses', () => {
  // The combos rendered on the live tooltip are kept from the same parse renderTip ran,
  // so a cap can be matched back to the shortcut it spells.
  assert.match(tooltipJs, /import \{ renderTip, parseTip \} from '\.\/tipContent\.js';/);
  assert.match(tooltipJs, /curCombos = parseTip\(txt\)\.keys;/);
  assert.match(tooltipJs, /curCombos = \[\];/, 'and cleared with the tooltip');
  // Matching key -> shake and KEEP the tooltip; anything else -> the old dismissal.
  assert.match(tooltipJs, /if \(e\.key !== 'Escape' && shakeMatchingKeys\(e\)\) return;\s*\n\s*hide\(\);/);
  assert.match(tooltipJs, /flashClass\(cap, SHAKE_CLASS, SHAKE_MS\)/);
  // Restart-safe, so pressing the same shortcut twice shakes twice.
  const flash = tooltipJs.slice(tooltipJs.indexOf('const flashClass ='));
  assert.match(flash, /classList\.remove\(cls\);\s*\n\s*void el\.offsetWidth;/);
  assert.match(flash, /el\.__flashTimer = setTimeout\(\(\) => el\.classList\.remove\(cls\), ms\);/);
});

test('the keycap shake is one brief, non-repeating pass', () => {
  const rule = componentsCss.match(/\.tip-key\.key-shake \{([\s\S]*?)\n\}/);
  assert.ok(rule, 'the shake is styled');
  const [, body] = rule;
  assert.match(body, /animation: keycapShake 0\.32s [^;]*both;/);
  assert.ok(!/infinite|alternate/.test(body), 'never loops');
  const frames = componentsCss.match(/@keyframes keycapShake \{([\s\S]*?)\n\}/);
  assert.ok(frames, 'and has its keyframes');
  assert.match(frames[1], /0%, 100% \{ transform: none; \}/, 'it starts and ends put');
});

// ── Reduced motion ──────────────────────────────────────────────────────────

test('reduced motion: the tooltip appears at once and the cap answers without moving', () => {
  const block = componentsCss.match(
    /@media \(prefers-reduced-motion: reduce\) \{[\s\S]*?#app-tooltip \{([\s\S]*?)\n    \}/);
  assert.ok(block, 'the tooltip opts out under the preference');
  assert.match(block[1], /transition: none;/);
  assert.match(block[1], /transform: none;/, 'and lands in its end state, not the offset one');
  assert.match(block[1], /--dissolve: 0;/, 'no half-sanded tooltip either');
  assert.match(block[1], /mask-image: none;/, 'the grain is off outright, not merely settled');
  assert.match(componentsCss, /\.tip-key\.key-shake \{ animation: none; \}/);
  // The cap shakes and nothing else: no repaint, so it reads as the same key throughout.
  const shake = componentsCss.match(/\.tip-key\.key-shake \{([\s\S]*?)\n\}/)[1];
  assert.doesNotMatch(shake, /border-color|color:|background/);
});

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

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const componentsCss = read('../css/components.css');
const tooltipJs = read('../js/ui/controlTooltip.js');

// A keydown as the DOM reports it. `code` is the PHYSICAL key, which is the only side
// that still says "A" when a Mac turns Alt+A into "å".
const press = (key, { code = '', ctrl = false, alt = false, shift = false, meta = false } = {}) =>
  ({ key, code, ctrlKey: ctrl, altKey: alt, shiftKey: shift, metaKey: meta });

// ── 1. Appear / disappear ───────────────────────────────────────────────────

test('the tooltip fades and rises in, on a transition rather than an animation', () => {
  const block = componentsCss.match(/#app-tooltip \{([\s\S]*?)\n\}/);
  assert.ok(block, '#app-tooltip is styled');
  assert.match(block[1], /opacity: 0;/, 'hidden at rest');
  assert.match(block[1], /transform: translateY\(5px\) scale\(0\.97\);/, 'and offset, so it rises in');
  assert.match(block[1], /transition: opacity \d+ms [^;]*, transform \d+ms/,
    'both properties transition');
  assert.ok(!/animation:/.test(block[1]),
    'no keyframes — a sweep across many controls would restart one mid-flight');
  assert.match(componentsCss, /#app-tooltip\.visible \{ opacity: 1; transform: none; \}/,
    'and `.visible` is the whole of the shown state, so removing it plays the exit');
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
  assert.match(tooltipJs, /const SHOW_DELAY_MS = 90;/);
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
    /@media \(prefers-reduced-motion: reduce\) \{\s*\n\s*#app-tooltip \{([\s\S]*?)\n\}/);
  assert.ok(block, 'the tooltip opts out under the preference');
  assert.match(block[1], /transition: none;/);
  assert.match(block[1], /transform: none;/, 'and lands in its end state, not the offset one');
  assert.match(componentsCss, /\.tip-key\.key-shake \{ animation: none; \}/);
  // The accent recolour is not motion, so it survives — the cap still answers.
  const shake = componentsCss.match(/\.tip-key\.key-shake \{([\s\S]*?)\n\}/)[1];
  assert.match(shake, /border-color: var\(--accent\);/);
  assert.match(shake, /color: var\(--accent\);/);
});

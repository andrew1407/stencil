// The tooltip keycap shake (js/ui/controlTooltip.js): a shortcut press nudges the cap that
// spells it instead of dismissing the tip, once, and never under reduced motion.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { parseCombo, eventCombo, comboMatchesEvent, dustOrigin, DUST_CURSOR_PX } from '../js/ui/tip/controlTooltip.js';
import { COMPONENTS_CSS } from './helpers/css.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const componentsCss = COMPONENTS_CSS;
const tooltipJs = read('../js/ui/tip/controlTooltip.js');

// A keydown as the DOM reports it. `code` is the PHYSICAL key, which is the only side
// that still says "A" when a Mac turns Alt+A into "å".
const press = (key, { code = '', ctrl = false, alt = false, shift = false, meta = false } = {}) =>
  ({ key, code, ctrlKey: ctrl, altKey: alt, shiftKey: shift, metaKey: meta });

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
  assert.match(tooltipJs, /import \{ renderTip, parseTip \} from '[^']*content\.js';/);
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

// A stretched control has its content at one end and its centre in empty space, so past DUST_CURSOR_PX
// the pointer is the origin, as AppTooltip.hpp's `stretched` test already does (user report).
test('the dust forms at the control centre, or at the cursor once that is far from it', () => {
  const centre = { x: 100, y: 100 };
  // A toolbar icon: the pointer is on it, so the centre IS the cursor, near enough.
  assert.deepEqual(dustOrigin(centre, { x: 108, y: 104 }), centre);
  assert.deepEqual(dustOrigin(centre, { x: 100 + DUST_CURSOR_PX, y: 100 }), centre,
    'exactly at the threshold still belongs to the control');
  // A wide row: the pointer is hundreds of pixels from the middle of it.
  const far = { x: 420, y: 104 };
  assert.deepEqual(dustOrigin(centre, far), far);
  // No pointer yet (a focus-driven reveal), or a detached owner with no box at all.
  assert.deepEqual(dustOrigin(centre, null), centre);
  assert.equal(dustOrigin(null, far), null);
});

// A descendant that describes ITSELF is a different tooltip, not the same one seen through its icon —
// the connections row's status dot inside the URL label.
test('a child with its own text takes the tooltip over from its ancestor', () => {
  assert.match(tooltipJs,
    /if \(curEl && curEl\.contains\(e\.target\) && \(!el \|\| el === curEl\)\) return;/,
    'the guard keeps showing only while the child has no text of its own');
});

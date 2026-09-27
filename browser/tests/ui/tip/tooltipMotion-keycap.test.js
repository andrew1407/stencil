// The tooltip keycap shake (js/ui/tip/controlTooltip.js): a shortcut press nudges the cap that
// spells it instead of dismissing the tip, once, and never under reduced motion. Driven on the
// stub page of tooltipRig.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parseCombo, eventCombo, comboMatchesEvent, dustOrigin, DUST_CURSOR_PX } from '../../../js/ui/tip/controlTooltip.js';
import { TIP_SHOW_DELAY_MS, TIP_DUST_IN_MS } from '../../../js/ui/motion.js';
import { COMPONENTS_CSS } from '../../helpers/css.js';
import { installTooltipPage } from '../../helpers/tooltipRig.js';

const componentsCss = COMPONENTS_CSS;
const page = await installTooltipPage();

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

const caps = () => page.tip.querySelectorAll('.tip-key');
const shaken = () => caps().map((c) => c.classList.contains('key-shake'));
const reveal = (text, { reduced = false } = {}) => {
  page.reset();
  page.page.reduced = reduced;
  const el = page.control(text);
  page.over(el);
  page.run(TIP_SHOW_DELAY_MS);
  return el;
};

test('every cap nudges once the tooltip has LANDED, announcing the shortcut', () => {
  // The shake's job is to draw the eye to the shortcut while you are READING the tip,
  // so it fires on the show — not only when the key happens to be pressed.
  reveal('Undo (Ctrl+Z / Ctrl+Shift+Z)');
  assert.deepEqual(shaken(), [false, false, false, false, false], 'still sand: nothing nudges yet');
  // …but only once the motes have arrived: a nudge played while the tip is still
  // assembling is a movement nobody can see, which is the whole point of it.
  assert.equal(page.run(TIP_DUST_IN_MS), 1, 'the shake waits out the gather');
  assert.deepEqual(shaken(), [true, true, true, true, true], 'every cap, both combos');
  reveal('Undo (Ctrl+Z)', { reduced: true });
  assert.deepEqual(shaken(), [true, true], 'and fires at once when there was no gather');
  // …and a tip dismissed or re-pointed mid-flight never shakes the caps of a tip that
  // has already gone: both routes drop the pending nudge first.
  reveal('Save (Ctrl+S)');
  page.fire('pointerdown', {});
  assert.equal(page.run(TIP_DUST_IN_MS), 0, 'a dismissal drops the pending nudge');
  reveal('Save (Ctrl+S)');
  page.over(page.control('Open (Ctrl+O)', { rect: { left: 300, top: 100, width: 30, height: 30 } }));
  page.run(TIP_SHOW_DELAY_MS);
  assert.equal(page.run(TIP_DUST_IN_MS), 1, 're-pointing leaves only the new tip\'s nudge');
  // A tip with no shortcut has no caps, so the query is empty and nothing happens.
  reveal('Plain words', { reduced: true });
  assert.deepEqual([page.visible(), caps().length], [true, 0], 'no special case for a shortcut-less tooltip');
});

test('the shake is wired to the tooltip’s own caps, one shot, and Escape still dismisses', () => {
  // The combos rendered on the live tooltip are kept from the same parse renderTip ran,
  // so a cap can be matched back to the shortcut it spells.
  reveal('Undo (Ctrl+Z / Ctrl+Shift+Z)', { reduced: true });
  caps().forEach((c) => c.classList.remove('key-shake'));
  // Matching key -> shake and KEEP the tooltip; anything else -> the old dismissal.
  page.key(press('Z', { code: 'KeyZ', ctrl: true, shift: true }));
  assert.equal(page.visible(), true, 'the matching shortcut keeps the tooltip up');
  assert.deepEqual(shaken(), [false, false, true, true, true], 'only the cap that spells it moves');
  assert.ok(page.delays().includes(340), 'one shot: the class comes off on its own');
  page.run(340);
  assert.deepEqual(shaken(), [false, false, false, false, false]);
  // Restart-safe, so pressing the same shortcut twice shakes twice.
  const cap = caps()[2];
  const ops = [];
  const { add, remove } = cap.classList;
  Object.assign(cap.classList, { add: (c) => { ops.push(`+${c}`); add(c); }, remove: (c) => { ops.push(`-${c}`); remove(c); } });
  page.key(press('z', { code: 'KeyZ', ctrl: true, shift: true }));
  page.key(press('z', { code: 'KeyZ', ctrl: true, shift: true }));
  assert.deepEqual(ops, ['-key-shake', '+key-shake', '-key-shake', '+key-shake'], 'the class is re-added, not stacked');
  page.key(press('x', { code: 'KeyX' }));
  assert.equal(page.visible(), false, 'any other key dismisses');
  page.key(press('z', { code: 'KeyZ', ctrl: true }));
  assert.deepEqual(shaken(), [false, false, true, true, true], 'and the cleared combos shake nothing');
  reveal('Close (Esc)', { reduced: true });
  page.key(press('Escape', { code: 'Escape' }));
  assert.equal(page.visible(), false, 'Escape dismisses, even when a cap spells it');
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
  const row = reveal('Server row');
  const icon = page.control('', { parent: row });
  page.over(icon);
  assert.deepEqual([page.visible(), page.delays().includes(TIP_SHOW_DELAY_MS)], [true, false],
    'an icon with no text of its own keeps its ancestor\'s tooltip');
  const dot = page.control('Connected', { parent: row });
  page.over(dot);
  assert.equal(page.visible(), false, 'a child with its own text drops the ancestor\'s');
  page.run(TIP_SHOW_DELAY_MS);
  assert.equal(page.visible(), true, 'and shows its own');
});

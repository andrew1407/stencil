// Rich control tooltips: the parse/render contract (js/ui/tipContent.js).
//
// The app never authored tooltip HTML — it composes ONE `title` string per control
// (utils.js composeControlTitle). These cases pin how that string becomes the desktop
// app's tooltip shape: heading + keycaps, term/description rows, bullets, hints and the
// muted disabled-reason note. extension/tests/tipContent.test.js runs the same cases on
// the extension's port, so the two can't drift.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { parseTip, renderTip, keysHtml, highlightKeys, isKeyCombo } from '../js/ui/tipContent.js';
import { COMPONENTS_CSS } from './helpers/css.js';

test('a trailing "(combo)" becomes keycaps, and only when it really is a combo', () => {
  const tip = parseTip('Fit to window (Alt+0)');
  assert.equal(tip.title, 'Fit to window');
  assert.deepEqual(tip.keys, ['Alt+0']);
  // The Mac rendering the hotkey registry produces (formatCombo → "⇧⌘Z").
  assert.deepEqual(parseTip('Undo (⇧⌘Z)').keys, ['⇧⌘Z']);
  assert.deepEqual(parseTip('Save name (Enter)').keys, ['Enter']);
  assert.deepEqual(parseTip('Close (Esc)').keys, ['Esc']);
  // Prose in parentheses is part of the heading, not a shortcut.
  const prose = parseTip('Incognito (choose before adding an image)');
  assert.deepEqual(prose.keys, []);
  assert.equal(prose.title, 'Incognito (choose before adding an image)');
});

test('keycaps: every key is its own cap, joined by "+"', () => {
  assert.equal(keysHtml('Ctrl+Shift+Z', false),
    '<kbd class="tip-key">Ctrl</kbd><span class="tip-plus">+</span>'
    + '<kbd class="tip-key">Shift</kbd><span class="tip-plus">+</span><kbd class="tip-key">Z</kbd>');
  // Apple prints ⇧⌘Z with no joiner, but a tooltip is read at a glance and a run of bare
  // glyphs reads as one symbol — so the glyph form is separated too.
  assert.equal(keysHtml('⇧⌘Z', false),
    '<kbd class="tip-key">⇧</kbd><span class="tip-plus">+</span>'
    + '<kbd class="tip-key">⌘</kbd><span class="tip-plus">+</span><kbd class="tip-key">Z</kbd>');
  assert.equal(keysHtml('⌥↑', false),
    '<kbd class="tip-key">⌥</kbd><span class="tip-plus">+</span><kbd class="tip-key">↑</kbd>');
  assert.ok(isKeyCombo('Alt+Shift+O') && isKeyCombo('Enter') && isKeyCombo('F5'));
  assert.ok(!isKeyCombo('add an image first') && !isKeyCombo(''));
});

test('inline combos are highlighted; the app\'s own verbs are not', () => {
  assert.match(highlightKeys('Alt+O cycles', false), /<kbd class="tip-key">Alt<\/kbd>/);
  // "hold Shift" is a key; "Delete every saved project" is a sentence.
  assert.match(highlightKeys('hold Shift for all 7 points', false), /hold <kbd class="tip-key">Shift<\/kbd>/);
  assert.equal(highlightKeys('Delete every saved project', false), 'Delete every saved project');
  assert.equal(highlightKeys('Clear selection', false), 'Clear selection');
});

test('a modifier paired with a gesture word is one combo, not a chopped-up key', () => {
  // The app writes real gestures this way: "Shift+click", "Alt+wheel", "Shift+left-drag".
  assert.match(highlightKeys('Shift+click: without theme', false),
    /<kbd class="tip-key">Shift<\/kbd><span class="tip-plus">\+<\/span><kbd class="tip-key">click<\/kbd>: without theme/);
  assert.match(highlightKeys('Alt+wheel zooms', false), /<kbd class="tip-key">wheel<\/kbd> zooms/);
  assert.match(highlightKeys('Shift+left-drag', false), /<kbd class="tip-key">left-drag<\/kbd>/);
  // A combo that ends on a modifier is whole too ("hold Alt+Shift").
  assert.match(highlightKeys('hold Alt+Shift', false), /<kbd class="tip-key">Alt<\/kbd>.*<kbd class="tip-key">Shift<\/kbd>$/);
});

test('every interpolated value is HTML-escaped', () => {
  const html = renderTip('Rename <img src=x onerror=alert(1)> & "quoted"');
  assert.ok(!html.includes('<img'), 'no raw markup survives');
  assert.match(html, /&lt;img src=x onerror=alert\(1\)&gt; &amp; &quot;quoted&quot;/);
});

test('the compare button: heading, bulleted rows, and a parenthesised key hint', () => {
  // The real title (browser/js/ui/toolbar.js): a "•" marker per mode, and NO "Alt+O
  // cycles" line — that shortcut is already the heading's keycap.
  const title = [
    'Compare with original',
    '• None — normal editing',
    '• Original — the original only (crop + rotation)',
    '• Vertical split — original left, edit right',
    '(hold Alt+Shift+O to peek)',
  ].join('\n');
  const tip = parseTip(title);
  assert.equal(tip.title, 'Compare with original');
  // The registry appends the shortcut to the END of the composed title, so the tooltip
  // has to find "(⌥O)" on the last line and still hang it off the heading.
  assert.deepEqual(parseTip(title + ' (⌥O)').keys, ['⌥O']);
  const rows = tip.blocks.filter(b => b.kind === 'row');
  assert.equal(rows.length, 3, 'each "term — description" line is one row');
  assert.deepEqual(rows[0], { kind: 'row', term: 'None', desc: 'normal editing' },
    'the "•" marker is stripped — CSS draws it, so it never lands in the text');
  const hints = tip.blocks.filter(b => b.kind === 'hint');
  assert.deepEqual(hints, [{ kind: 'hint', text: 'Hold Alt+Shift+O to peek' }]);
  // A row's description keeps its own "·" list instead of being torn into bullets.
  assert.equal(parseTip('x\nV split — left: original · right: current edit').blocks[0].desc,
    'left: original · right: current edit');
  const html = renderTip(title, false);
  assert.equal((html.match(/class="tip-rows"/g) || []).length, 1, 'consecutive rows share one grid');
  // The two columns only line up while term/desc are DIRECT children of that grid.
  assert.match(html, /<div class="tip-rows"><span class="tip-term">/);
  assert.ok(!html.includes('class="tip-row"'), 'no wrapper element between grid and cells');
  assert.match(html, /<kbd class="tip-key">Alt<\/kbd>/, 'the hint keeps its keycaps');
});

test('a single "·" piece on the heading line becomes a hint, not a bullet of one', () => {
  const tip = parseTip('Drag to reorder · drag out of the modal to disconnect');
  assert.equal(tip.title, 'Drag to reorder');
  assert.deepEqual(tip.blocks, [{ kind: 'hint', text: 'Drag out of the modal to disconnect' }]);
});

test('two+ "·" pieces on the heading line become bullets under the heading', () => {
  const tip = parseTip('a · b · c');
  assert.equal(tip.title, 'a');
  assert.deepEqual(tip.blocks, [{ kind: 'bullet', text: 'b' }, { kind: 'bullet', text: 'c' }]);
  const html = renderTip('a · b · c', false);
  assert.equal((html.match(/<ul class="tip-bullets">/g) || []).length, 1, 'one list, not one per bullet');
  assert.equal((html.match(/<li>/g) || []).length, 2);
});

test('"term — description" on the heading line splits into heading + muted subtitle', () => {
  const tip = parseTip('Shared server project — https://stencil.example/p/1');
  assert.equal(tip.title, 'Shared server project');
  assert.deepEqual(tip.blocks, [{ kind: 'text', text: 'https://stencil.example/p/1', muted: true }]);
  assert.match(renderTip('Shared server project — https://stencil.example/p/1'), /class="tip-hint"/);
});

test('the disabled-reason line composeControlTitle appends renders as the note', () => {
  const tip = parseTip('Crop image (Alt+R)\n— add an image first');
  assert.deepEqual(tip.keys, ['Alt+R']);
  assert.deepEqual(tip.blocks, [{ kind: 'note', text: 'Add an image first' }]);
  assert.match(renderTip('Crop image\n— add an image first'), /<div class="tip-note">Add an image first<\/div>/);
});

test('a combo appended after a multi-line body still becomes the heading\'s keycap', () => {
  // What composeControlTitle really produces for the compare control on a Mac.
  const tip = parseTip('Compare with original\nNone — normal editing\n(Alt+O cycles) (⌥O)\n— Load an image to compare');
  assert.equal(tip.title, 'Compare with original');
  assert.deepEqual(tip.keys, ['⌥O']);
  const hints = tip.blocks.filter(b => b.kind === 'hint');
  assert.deepEqual(hints, [{ kind: 'hint', text: 'Alt+O cycles' }], 'the parens are the hint\'s, not text');
  assert.deepEqual(tip.blocks.at(-1), { kind: 'note', text: 'Load an image to compare' });
  // A line that was NOTHING but the combo disappears with it.
  const only = parseTip('Fit to window\n(⌥0)');
  assert.deepEqual(only.keys, ['⌥0']);
  assert.deepEqual(only.blocks, []);
});

test('empty / whitespace titles render nothing at all', () => {
  for (const v of ['', '   ', '\n\n', null, undefined]) assert.equal(renderTip(v), '');
});

test('the tooltip controller renders the structure, and the CSS styles every part', () => {
  const js = readFileSync(new URL('../js/ui/controlTooltip.js', import.meta.url), 'utf8');
  assert.match(js, /renderTip/, 'the controller goes through the content model');
  assert.ok(!/\.textContent\s*=\s*txt/.test(js), 'and no longer prints the title flat');
  const css = COMPONENTS_CSS;
  for (const cls of ['.tip-head', '.tip-title', '.tip-keys', '.tip-key', '.tip-plus',
    '.tip-rows', '.tip-term', '.tip-desc', '.tip-bullets', '.tip-hint', '.tip-note']) {
    assert.ok(css.includes(cls), `${cls} is styled`);
  }
  assert.ok(!/#app-tooltip\s*\{[^}]*white-space:\s*pre-line/.test(css),
    'the flat-text fallback is gone — the tooltip is real markup now');
});

test('the platform key vocabulary: Qt\'s macOS glyphs, and native modifiers on a Mac', () => {
  // The desktop composes its shortcuts with QKeySequence::NativeText, which on macOS
  // renders Escape as ⎋, Tab as ⇥, Page Up as ⇞, Home as ↖, Delete as ⌦ … If the parser
  // does not know those, the trailing "(⎋)" is never recognised and stays as text.
  for (const k of ['⎋', '⇥', '↵', '⌤', '⇞', '⇟', '↖', '↘', '⌫', '⌦', '⌘⇥', '⇧⌘S', '⌥R', '⌘⌦'])
    assert.ok(isKeyCombo(k), `${k} is a shortcut`);
  assert.deepEqual(parseTip('Close (⎋)').keys, ['⎋'], 'a lone glyph is lifted off the heading');
  // …but a lone arrow in PROSE is a separator, not a key, and must be left alone.
  assert.equal(highlightKeys('npm run serve → localhost', false), 'npm run serve → localhost');

  // A hand-written combo is drawn natively: spelled out on Windows/Linux, glyphed on a Mac.
  assert.match(keysHtml('Alt+R', false), /<kbd class="tip-key">Alt<\/kbd>/);
  assert.match(keysHtml('Alt+R', true), /<kbd class="tip-key">⌥<\/kbd>/);
  assert.match(keysHtml('Alt+Shift+R', true), /⌥<\/kbd>.*⇧<\/kbd>.*R<\/kbd>/);
  // Ctrl maps to ⌃, NOT ⌘: one that survives to render time is a literal Control key —
  // the hotkey registry has already turned its own Ctrl bindings into ⌘ before this.
  assert.match(keysHtml('Ctrl+V', true), /<kbd class="tip-key">⌃<\/kbd>/);
  // An already-glyphed combo is never mapped twice.
  assert.equal(keysHtml('⇧⌘S', true), keysHtml('⇧⌘S', false));
});

// A secondary line reads as a sentence of its own: "Servers — a saved session expired,
// reconnect to sign in again" showed a lowercase fragment under the heading, as if
// someone had forgotten to finish it (user report, with a picture). What must NOT be
// lifted is anything whose first token is a value rather than a word.
test('a secondary line is sentence-cased; values and code fragments are left alone', () => {
  // The first block under the heading, whatever it is called: the muted subtitle after a
  // dash is a `text` block here and a hint on the desktop, and the rule is the same for
  // every one of them.
  const hintOf = (s) => parseTip(s).blocks[0]?.text;
  const noteOf = hintOf;

  assert.equal(hintOf('Servers — a saved session expired, reconnect to sign in again'),
    'A saved session expired, reconnect to sign in again');
  assert.equal(noteOf('Crop image\n— add an image first'), 'Add an image first');
  assert.equal(hintOf('Drag to reorder · drag out of the modal to disconnect'),
    'Drag out of the modal to disconnect');

  // A URL, a filename, an identifier, a code fragment: written as they mean.
  assert.equal(hintOf('Shared server project — http://localhost:8090'), 'http://localhost:8090');
  assert.equal(hintOf('Open Project — .stencil files only'), '.stencil files only');
  assert.equal(hintOf('Formula — f(x,y) transforms the page'), 'f(x,y) transforms the page');
  assert.equal(hintOf('Facade — stencil.voiceChat toggles it'), 'stencil.voiceChat toggles it');
  // A LONE word is a value (an axis letter, a mode name), not a sentence.
  assert.equal(hintOf('Axis · x'), 'x');
  // Already capitalised, or not a letter at all: untouched.
  assert.equal(hintOf('Zoom — 100% of the original'), '100% of the original');
  assert.equal(hintOf('Servers — Reconnect to sign in again'), 'Reconnect to sign in again');
});

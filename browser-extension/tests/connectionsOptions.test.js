// The options page's connection list: the admin cue, the kind pills and the row chrome, read as
// source text, plus the filter transition its wholesale rebuild plays.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { filterConnections } from '../src/lib/connection/connections.js';
import { createFilterTransition } from '../src/lib/motion.js';
import { makeList, renderKeys } from './helpers/listDom.js';

// ── The options page wires the kind cue + filter ──
test('options page: golden admin outline and the three-way kind filter', () => {
  const html = readFileSync(new URL('../src/options/options.html', import.meta.url), 'utf8');
  const js = readFileSync(new URL('../src/options/connections.js', import.meta.url), 'utf8');
  // Gold outline, the same #f5c518 cue a server-backed pin wears.
  assert.match(html, /\.pin-row\.conn-admin\s*\{[^}]*#f5c518/);
  assert.match(js, /conn-admin/);
  assert.match(js, /can mint session tokens/, 'the row explains what admin means');
  // Three .chk accent pills, one choice (radios), All checked by default.
  for (const v of ['all', 'admin', 'other'])
    assert.match(html, new RegExp(`<input type="radio" name="conn-kind" value="${v}"`));
  assert.match(html, /value="all" checked/);
  assert.match(js, /filterConnections\(all, connKind\(\)\)/, 'the render filters the list');
});

// Source-level pins: the row markup and the CSS it leans on live in two files with no DOM to
// assert against under `node --test`, so the contract is checked as text.
const optionsHtml = () => readFileSync(new URL('../src/options/options.html', import.meta.url), 'utf8');
// Just the connections module — the pins renderer beside it has its own buttons.
const connectionsJs = () =>
  readFileSync(new URL('../src/options/connections.js', import.meta.url), 'utf8');

test('options page: removing a connection uses the destructive trash glyph', () => {
  const js = connectionsJs();
  assert.match(js, /remove\.innerHTML = icon\('trash'/, 'a delete uses the trash icon, not an x');
  assert.doesNotMatch(js, /remove\.innerHTML = icon\('x'/);
  assert.match(js, /remove\.className = 'pin-btn danger'/, 'it keeps the danger colour');
  assert.match(js, /setTip\(remove, 'Remove connection/);
});

test('options page: row action buttons read as enabled at rest, disabled only when disabled', () => {
  const html = optionsHtml();
  // Resting face + border are accent-tinted (not the flat panel/line that read as greyed).
  assert.match(html, /\.pin-btn \{[^}]*background:color-mix\(in srgb, var\(--btn-ink\) \d+%, var\(--panel\)\)/);
  assert.match(html, /\.pin-btn \{[^}]*color:var\(--btn-ink\)/);
  assert.match(html, /\.pin-btn \{[^}]*border:1px solid color-mix\(in srgb, var\(--btn-ink\)/);
  // Dark borrows the lighter accent shade, like the .chk pills in lib/theme/controls.css.
  assert.match(html, /:root\[data-theme="dark"\] \{ --btn-ink:var\(--accent-2\)/);
  // Hover is the enhancement (full accent fill) and never fires on a disabled button.
  assert.match(html, /\.pin-btn:hover:not\(:disabled\) \{[^}]*background:var\(--accent\)/);
  assert.match(html, /\.pin-btn:disabled \{[^}]*opacity:\.45[^}]*color:var\(--muted\)/);
});

test('options page: the admin badge sits at the RIGHT, beside the action buttons', () => {
  const js = connectionsJs();
  // A row child between .pin-info and .pin-actions — NOT inside .conn-name, where it
  // would trail the host label and eat the ellipsis budget of a long one.
  assert.match(js, /li\.append\(info, \.\.\.\(badge \? \[badge\] : \[\]\), actions\);/);
  assert.doesNotMatch(js, /name\.append\(badge\)/);
  assert.match(js, /setTip\(badge, 'Admin credential/, 'it keeps its explanation');
  const html = optionsHtml();
  // Still the gold cue, and it never shrinks or wraps as the host label grows.
  assert.match(html, /\.pin-badge-server \{[^}]*color:#f5c518/);
  assert.match(html, /\.conn-badge-admin \{[^}]*flex:none[^}]*white-space:nowrap/);
  assert.match(html, /\.conn-label \{[^}]*text-overflow:ellipsis/, 'the host still ellipsises');
});

test('options page: the connection row is a .pin-row sibling with its own padding', () => {
  assert.match(connectionsJs(), /li\.className = 'pin-row conn-row'/);
  const html = optionsHtml();
  assert.match(html, /\.pin-row\.conn-row \{[^}]*padding:/, 'the thumb-less row sets its own padding');
  assert.match(html, /\.pin-row:hover \{[^}]*border-color:/, 'rows share one hover cue');
});

test('options page: the kind pills are compact and the connect row wraps as a unit', () => {
  const html = optionsHtml();
  assert.match(html, /<div class="row conn-bar">/);
  assert.match(html, /\.row\.conn-bar \{[^}]*flex-wrap:wrap/, 'the pills drop to their own line, not onto the buttons');
  // Popup format-pill sizing (popup.css .formats .chk), right-aligned on the row.
  assert.match(html, /\.conn-filters \.chk \{[^}]*font-size:11px/);
  assert.match(html, /\.conn-filters \{[^}]*margin-left:auto/);
});

// The options list is rebuilt wholesale on every kind-pill change, so what leaves and arrives is
// decided by the keys of the two renders — each row's data-url. Mechanics: motion.test.js.

const CONNS = [
  { url: 'http://a', credentialKind: 'admin' },
  { url: 'http://b', credentialKind: '' },
  { url: 'http://c' },
];
const urlsFor = (mode) => filterConnections(CONNS, mode).map((c) => c.url);

test('switching the kind filter drops exactly the excluded rows, and brings them back', () => {
  const list = makeList();
  const tr = createFilterTransition({ list, keyAttr: 'url', reduced: () => false });

  renderKeys(list, tr, urlsFor('all'), { attr: 'url' });
  const toAdmin = renderKeys(list, tr, urlsFor('admin'), { attr: 'url' });
  assert.deepEqual(toAdmin, { entered: [], left: ['http://b', 'http://c'] });
  assert.equal(tr.ghostCount, 2, 'the non-admin rows play out instead of blinking away');

  const backToAll = renderKeys(list, tr, urlsFor('all'), { attr: 'url' });
  assert.deepEqual(backToAll, { entered: ['http://b', 'http://c'], left: [] },
    're-admitted rows ENTER — the filter reads the same in both directions');
  assert.equal(tr.ghostCount, 0, 'the interrupted exits were dropped, not left under the new rows');
  assert.deepEqual(list.keys('url'), ['http://a', 'http://b', 'http://c']);
});

test('reduced motion: the kind filter still lands on exactly the right rows', () => {
  const list = makeList();
  const tr = createFilterTransition({ list, keyAttr: 'url', reduced: () => true });
  renderKeys(list, tr, urlsFor('all'), { attr: 'url' });
  renderKeys(list, tr, urlsFor('other'), { attr: 'url' });
  assert.deepEqual(list.keys('url'), ['http://b', 'http://c']);
  assert.equal(tr.ghostCount, 0);
});

test('options page: the two lists animate filter changes, and a delete still scatters', () => {
  // The pin list and the connection list live in their own modules now; read them together.
  const js = ['pinsDom.js', 'pins.js', 'pinRow.js', 'connections.js']
    .map((f) => readFileSync(new URL(`../src/options/${f}`, import.meta.url), 'utf8')).join('\n');
  // Both lists are wrapped: snapshot before the wipe, play after the rebuild.
  assert.match(js, /createFilterTransition\(\{ list: pinListEl \}\)/);
  assert.match(js, /createFilterTransition\(\{ list: connListEl, keyAttr: 'url' \}\)/);
  assert.match(js, /pinTransition\.begin\(\);\s*\n\s*pinListEl\.innerHTML = '';/);
  assert.match(js, /connTransition\.begin\(\);\s*\n\s*connListEl\.innerHTML = '';/);
  // A DELETE keeps the heavier effect and leaves the DOM, so it never also fades
  // out as if a filter had merely excluded it.
  assert.match(js, /leaveThenRemove\(li, \(\) => li\.remove\(\), scatterGridFor\(1\)\)/);
  // A freshly added connection is materialized by the add flow, not ramped in twice.
  assert.match(js, /connTransition\.end\(\{ skipEnter: materializingUrl/);
});

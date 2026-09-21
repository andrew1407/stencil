// components.css for the connections list: the compact batch strip, the trash buttons, and a
// row that reads as a sibling of a project row — idioms borrowed from the projects modal.
import { test } from 'node:test';
import assert from 'node:assert';
import { layout } from '../../js/ui/layout.js';
import { LAYOUT_CSS, COMPONENTS_CSS } from '../helpers/css.js';
import { conn, openModal, rows, find, markup } from '../helpers/connectModalRig.js';

// The connections list borrows the projects modal's row idioms: the trash can, filled
// row-action buttons enabled-looking at rest, .project-row's box, a heading-sized filter.

const cssText = () => COMPONENTS_CSS;
// A rule body by its exact opening selector text (line breaks in the selector list
// make a single regex brittle).
const ruleAfter = (css, selector) => {
  const at = css.indexOf(selector);
  assert.ok(at > -1, `${selector} present`);
  return css.slice(at, css.indexOf('}', at) + 1);
};

// Reconnect/Disconnect sit one level down (.connect-batch-selected), so a direct-child
// padding rule left them 4px taller than Select all and the bar grew (user report).
test('every button in the batch strip shares the compact padding, so the bar never grows', () => {
  const css = cssText();
  const rule = ruleAfter(css, '.connect-batch-actions button {');
  assert.match(rule, /padding: 6px 10px;/);
  assert.ok(!css.includes('.connect-batch-actions > button {'), 'a descendant rule, not direct children only');
});

test('the row disconnect is a trash button, like the projects modal’s remove', () => {
  const { list } = openModal([conn('http://plain:2', '')]);
  const disc = find(rows(list)[0], 'connect-disconnect');
  assert.ok(disc, 'every row can disconnect');
  assert.ok(disc.innerHTML.includes('ic-trash'), 'trash — this app’s remove glyph everywhere else');
  assert.ok(!disc.innerHTML.includes('ic-x'), 'the ✕ meant "close", not "forget this server"');
  assert.match(disc.dataset.title, /disconnect \(and forget\) this server/i, 'the tooltip says it forgets');
  assert.ok(disc.classList.contains('danger'), 'and it keeps the danger treatment');
});

test('the batch bar disconnects with the same trash; Deselect all is its only clear', () => {
  const bar = /<div class="connect-batch-bar"[\s\S]*?<\/div>/.exec(markup)?.[0] ?? '';
  const btn = (id) => new RegExp(`<button id="${id}"[\\s\\S]*?</button>`).exec(bar)?.[0] ?? '';
  const disc = btn('connect-batch-disconnect');
  assert.ok(disc.includes('ic-trash'), 'the row and the batch bar agree on the glyph');
  assert.ok(!disc.includes('ic-x'));
  assert.ok(disc.includes('class="danger'), 'still the danger colour');
  assert.strictEqual(btn('connect-batch-clear'), '',
    'no separate Clear — it did exactly what Deselect all does (user decision)');
  assert.ok(btn('connect-select-all').includes('ic-check'));
});

test('components.css: a connection row and a project row look like siblings', () => {
  const css = cssText();
  const row = /^\.connect-row \{([^}]*)\}/m.exec(css)?.[1] ?? '';
  const proj = /^\.project-row \{([^}]*)\}/m.exec(css)?.[1] ?? '';
  for (const decl of ['gap: 12px', 'padding: 8px 10px', 'border: 1px solid var(--border-main)',
    'border-radius: 8px', 'margin-bottom: 8px', 'background: var(--input-bg)']) {
    assert.ok(proj.includes(decl), `.project-row sets ${decl}`);
    assert.ok(row.includes(decl), `.connect-row matches it — ${decl}`);
  }
  assert.match(css, /\.connect-row:hover \{ background: var\(--bg-info\); \}/,
    'and the same hover lift .project-row:hover has');
});

test('components.css: an idle row button reads as enabled, not disabled', () => {
  const css = cssText();
  const btn = ruleAfter(css, '.connect-row .connect-reconnect-one,');
  // The row action is a plain button wearing layout.css's shared filled chrome; these rules
  // may only re-size it — a resting repaint in the muted palette reads as greyed-out.
  for (const muted of ['--disabled-bg', '--disabled-text', '--bg-info', '--text-muted', 'background']) {
    assert.ok(!btn.includes(muted), `nothing mutes them at rest (${muted})`);
  }
  assert.ok(!/border:/.test(btn), 'no bordered ghost variant either');
  assert.ok(btn.includes('padding: 5px 8px'), 'compact is the only difference');
  // Genuinely disabled still looks disabled — via the shared rule, not a local override.
  const layout = LAYOUT_CSS;
  assert.match(layout, /button:disabled,[\s\S]*?background: var\(--disabled-bg\)/);
  assert.ok(!css.includes('.connect-row .connect-disconnect:disabled'),
    'the row buttons defer to it');
});

test('components.css: the credential filter is compact and cannot crowd the heading', () => {
  const css = cssText();
  const rowRule = ruleAfter(css, '.connect-section-row {');
  assert.ok(rowRule.includes('flex-wrap: wrap'),
    'the filter drops under the heading before it can overlap it');
  assert.match(css, /\.connect-section-row \.vs-section \{[^}]*min-width: 0/,
    'and a long heading shrinks rather than shoving the filter out of the modal');
  // customSelect.js hides the native <select> behind an .accent-dd-trigger, so sizing
  // the select alone changed nothing on screen — the trigger is the control.
  const compact = ruleAfter(css, '.connect-section-row .modal-filter,');
  assert.ok(compact.includes('.cs-dd .accent-dd-trigger'), 'the enhanced trigger is sized too');
  assert.ok(compact.includes('font-size: 12px'), 'the .vs-section heading’s scale, not a form field’s');
  assert.ok(compact.includes('padding: 3px 8px'));
  assert.ok(compact.includes('max-width: 100%'), 'never wider than the modal');
});

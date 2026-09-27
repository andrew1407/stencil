// The context menu's "Stencil Script ▸" entry as markup and structure: the row, the flyout's
// ids and actions, its layer (no llm, no facade of its own) and the stylesheet that sizes it.
// The flyout's behaviour is driven in ctxScript.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';

import { layout } from '../../../js/ui/layout.js';
import { scriptFlyoutHtml } from '../../../js/ui/ctx/scriptItem.js';
import { COMPONENTS_CSS } from '../../helpers/css.js';

const MARKUP = layout();
const FLYOUT = scriptFlyoutHtml();
// ui/ is split into feature folders, so a bare module name is looked up, not assumed flat.
const UI_DIR = new URL('../../../js/ui/', import.meta.url);
const uiPath = (n) => {
  const walk = (d) => readdirSync(d, { withFileTypes: true }).flatMap((e) =>
    e.isDirectory() ? walk(new URL(`${e.name}/`, d)) : (e.name === n ? [new URL(e.name, d)] : []));
  return n.includes('/') ? new URL(n, UI_DIR) : walk(UI_DIR)[0];
};
const src = (name) => readFileSync(uiPath(name), 'utf8');
const IDS = ['ctx-script-sub', 'ctx-script-pane', 'ctx-script-wrap', 'ctx-script-highlight',
  'ctx-script-editor', 'ctx-script-diag', 'ctx-script-copy', 'ctx-script-download',
  'ctx-script-upload', 'ctx-script-upload-btn', 'ctx-script-clear', 'ctx-script-run'];

// ── The row and the flyout's shape ───────────────────────────────────────────
test('the menu row is a submenu parent, still naming the window shortcut', () => {
  const row = MARKUP.slice(MARKUP.indexOf('id="ctx-script"'), MARKUP.indexOf('id="ctx-draw-toggle"'));
  assert.match(row, /data-hk="openScript"/, 'Alt+Shift+S still opens the window');
  assert.match(row, /<span class="ctx-arrow"><svg class="ic ic-chevron-right"/, 'a caret, like Style');
  // Hung off the row on the first open that can use it, never written into layout().
  for (const id of IDS) assert.equal(MARKUP.split(`id="${id}"`).length - 1, 0, `${id} is not static`);
});

test('the flyout carries each of its ids once, and never one of the window\'s', () => {
  for (const id of IDS) assert.equal(FLYOUT.split(`id="${id}"`).length - 1, 1, `${id} appears once`);
  // The window and the flyout must be able to stand open together.
  for (const id of ['script-editor', 'script-highlight', 'script-diag', 'script-run']) {
    assert.ok(!FLYOUT.includes(`id="${id}"`), `${id} belongs to the window alone`);
  }
  // It reuses the window's editor classes, only re-sized for the menu (ctxScript.css).
  assert.match(FLYOUT, /<pre class="script-highlight"/);
  assert.match(FLYOUT, /class="script-input"/);
  assert.match(FLYOUT, /id="ctx-script-upload" accept="\.stc"/);
});

test('the actions are the window\'s five, Run leading and Clear the danger tail', () => {
  const order = [...FLYOUT.matchAll(/id="(ctx-script-(?:copy|download|upload|upload-btn|clear|run))"/g)].map((m) => m[1]);
  assert.deepEqual(order, ['ctx-script-run', 'ctx-script-copy', 'ctx-script-download',
    'ctx-script-upload', 'ctx-script-upload-btn', 'ctx-script-clear']);
  assert.match(FLYOUT, /id="ctx-script-run" class="btn-icon-text primary"/);
  // Clear throws work away, so it wears the danger red and sits past Run, out of the way.
  assert.match(FLYOUT, /id="ctx-script-clear" class="btn-icon-text danger"/);
  assert.match(FLYOUT, /id="ctx-script-clear"[^>]*>.*ic-trash/);
});

test('the flyout is a ui/ module: no llm, no facade of its own', () => {
  for (const name of ['ctx/script.js', 'ctx/scriptItem.js', 'ctx/scriptEditor.js',
    'script/editor.js', 'script/highlight.js']) {
    assert.ok(!/from '\.\.\/llm\//.test(src(name)), `${name} may not reach the llm layer`);
    assert.ok(!/window\.stencil/.test(src(name)), `${name} runs scripts through console/scriptRunner.js`);
  }
});

test('components.css gives the flyout a real editor window, sized for the menu', () => {
  assert.match(COMPONENTS_CSS, /\.ctx-script-plain > \.ctx-sub \{ display: none !important; \}/);
  const pane = COMPONENTS_CSS.slice(COMPONENTS_CSS.indexOf('.ctx-script {'), COMPONENTS_CSS.indexOf('#ctx-script-wrap'));
  assert.match(pane, /height: min\(\d+vh, \d+px\);/, 'a tall box, scaled to the viewport');
  assert.match(pane, /user-select: text;/, 'the menu is user-select:none; code is not');
  assert.match(COMPONENTS_CSS, /#ctx-script-wrap \{ flex: 1 1 0;/, 'the editor takes what is left over');
});

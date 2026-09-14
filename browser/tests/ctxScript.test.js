// The context menu's "Stencil Script ▸" entry: an ordinary submenu parent whose FLYOUT is a
// compact twin of the script window. Modelled on ctx-assistant.test.js and metaModals.test.js:
// the markup is pinned as text, the behaviour is driven through the DOM-lite stubs.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { layout } from '../js/ui/layout.js';
import { scriptFlyoutHtml } from '../js/ui/ctxScriptItem.js';
import { wireCtxScript } from '../js/ui/ctxScript.js';
import { setScriptText } from '../js/ui/scriptBuffer.js';
import { ctxKeepsTab } from '../js/ui/ctxKeyboard.js';
import { createStubElement, installDom } from './helpers/dom.js';
import { COMPONENTS_CSS } from './helpers/css.js';

const MARKUP = layout();
const FLYOUT = scriptFlyoutHtml();
const src = (name) => readFileSync(new URL(`../js/ui/${name}`, import.meta.url), 'utf8');
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

// ── The rig: a menu row the wiring can hang its flyout off ───────────────────
const asPre = (el) => {
  return el;
};
// Every id the inserted markup declares becomes a stub, so a missing one fails the wiring.
const registerIds = (doc, html) => {
  for (const [, id] of html.matchAll(/id="([a-z-]+)"/g)) {
    const tag = id.endsWith('editor') ? 'textarea' : id.endsWith('highlight') ? 'pre' : 'div';
    const el = createStubElement(tag, { id });
    doc.register(id, tag === 'pre' ? asPre(el) : el);
  }
};

const rig = ({ touch = false, stencil = {} } = {}) => {
  setScriptText('');   // the buffer outlives one test; each rig starts from an empty page
  const opened = [];
  const doc = installDom({}, { window: { stencil }, matchMedia: () => ({ matches: touch }) });
  doc.createTextNode = (text) => ({ nodeType: 3, textContent: text });
  const item = createStubElement('div', { id: 'ctx-script' });
  item.insertAdjacentHTML = (where, html) => { item.inserted = { where, html }; registerIds(doc, html); };
  item.querySelector = () => doc.getElementById('ctx-script-sub');
  doc.register('ctx-script', item);
  doc.register('script-btn', createStubElement('button', { click: () => opened.push('window') }));
  const host = {
    subs: [], closes: 0, closedSubs: 0, placed: [], busy: 0, sending: [],
    wireSubmenu: (i, s) => host.subs.push([i, s]),
    menuIsOpen: () => true,
    closeMenu: () => { host.closes += 1; },
    closeAllSubs: () => { host.closedSubs += 1; },
    positionSub: (i, s) => host.placed.push([i, s]),
    setActiveSub: () => {},
    setSending: (v) => host.sending.push(v),
    bumpBusy: () => { host.busy += 1; },
  };
  const app = { export: { downloadBlob: () => {} } };
  const api = wireCtxScript(app, host);
  return { doc, item, host, api, opened, restore: () => { api.dropEditor(); doc.restore(); } };
};
const settle = () => new Promise((r) => setTimeout(r, 0));

test('the flyout is built once, on the first sync that can use it, and wired as a submenu', (t) => {
  const { doc, item, host, api, restore } = rig();
  t.after(restore);
  assert.equal(item.inserted.where, 'beforeend', 'it hangs off the row, after the arrow');
  assert.equal(host.subs.length, 1, 'wired through the menu\'s own submenu machinery');
  assert.equal(host.subs[0][1], doc.getElementById('ctx-script-sub'));
  assert.equal(item.dataset.noSub, '0');
  api.syncScript();
  api.syncScript();
  assert.equal(host.subs.length, 1, 'built once, then only re-moded');
});

test('plain mode rides the app-wide touch rule and hands over to the window', (t) => {
  const { item, host, opened, restore } = rig({ touch: true });
  t.after(restore);
  assert.equal(item.dataset.noSub, '1');
  assert.ok(item.classes.has('ctx-script-plain'), 'no caret, no flyout');
  item.dispatch('click');
  assert.deepEqual(opened, ['window'], 'the full script window opens instead');
  assert.equal(host.closes, 1, 'and the menu gets out of its way');
});

// ── Inside the flyout ────────────────────────────────────────────────────────
const typed = (doc, text) => {
  const editor = doc.getElementById('ctx-script-editor');
  editor.value = text;
  editor.dispatch('input');
  return editor;
};
const painted = (doc) => doc.getElementById('ctx-script-highlight').childNodes
  .filter((n) => n.className).map((n) => n.className);

test('typing paints the core\'s own tokens as spans, synchronously', (t) => {
  const { doc, restore } = rig();
  t.after(restore);
  typed(doc, '@crop 10%');
  assert.deepEqual(painted(doc), ['stk-directive', 'stk-number', 'stk-unit', 'stk-punct']);
});

test('a bad directive is reported only once the script has been RUN', async (t) => {
  const { doc, restore } = rig();
  t.after(restore);
  const strip = doc.getElementById('ctx-script-diag');
  typed(doc, '@nope 5');
  assert.equal(strip.textContent, '', 'a half-typed line is not a mistake');
  assert.ok(!painted(doc).some((c) => c.includes('stk-error')));

  doc.getElementById('ctx-script-run').dispatch('click');
  await settle();
  assert.equal(strip.textContent, 'Line 1:1 — unknown directive \'@nope\'');
  assert.equal(strip.className, 'script-diag script-diag-error');
  assert.ok(painted(doc).some((c) => c.includes('stk-error')), 'and the token is underlined');
  // Editing again drops the verdict: it was about older text.
  typed(doc, '@nope 6');
  assert.equal(strip.textContent, '');
});

test('Run drives the script through the console facade, and the menu stays open', async (t) => {
  const crops = [];
  const { doc, host, restore } = rig({ stencil: { crop: (spec) => crops.push(spec) } });
  t.after(restore);
  typed(doc, '@crop 10%');
  doc.getElementById('ctx-script-run').dispatch('click');
  await settle();
  assert.equal(crops.length, 1, 'runScriptHere reached window.stencil');
  assert.equal(host.closes, 0, 'running never dismisses the menu');
  assert.deepEqual(host.sending, [true, false], 'the run marks the menu busy for the scroll guard');
  assert.ok(host.busy > 0, 'and holds the grace open while the canvas settles');
});

test('a failing script leaves the text, the menu and the flyout exactly where they were', async (t) => {
  const { doc, host, restore } = rig({ stencil: { crop: () => { throw new Error('nope'); } } });
  t.after(restore);
  typed(doc, '@crop 10%');
  doc.getElementById('ctx-script-run').dispatch('click');
  await settle();
  assert.equal(doc.getElementById('ctx-script-editor').value, '@crop 10%');
  assert.equal(host.closes, 0);
  assert.deepEqual(host.sending, [true, false]);
});

test('Run, Copy, Download and Clear need something to act on; Upload always does', (t) => {
  const { doc, restore } = rig();
  t.after(restore);
  const state = () => ['ctx-script-run', 'ctx-script-copy', 'ctx-script-download',
    'ctx-script-clear', 'ctx-script-upload-btn'].map((id) => doc.getElementById(id).disabled);
  assert.deepEqual(state(), [true, true, true, true, false], 'an empty editor gates the four');
  typed(doc, '@save');
  assert.deepEqual(state(), [false, false, false, false, false]);
  typed(doc, '   \n  ');
  assert.deepEqual(state(), [true, true, true, true, false], 'whitespace is not a script');
});

// ── The rules that make an editor in a menu possible ─────────────────────────
test('the flyout counts as engaged while a caret is in it or a script runs', () => {
  const code = src('ctxScriptEditor.js');
  assert.ok(code.includes('flyout._keepOpen = () => running || flyout.contains(document.activeElement);'),
    'the hover-out timers must not yank the editor away mid-script');
  assert.equal((code.match(/host\.closeMenu/g) || []).length, 1,
    'the touch hand-over to the window is the ONLY thing that closes the menu');
});

test('the editor keeps Tab for itself — it indents, it does not leave', () => {
  assert.match(FLYOUT, /data-ctx-keep-tab="1"/);
  assert.ok(ctxKeepsTab({ dataset: { ctxKeepTab: '1' } }));
  assert.ok(!ctxKeepsTab({ dataset: {} }), 'every other control lets Tab walk the flyout');
  assert.match(src('ctxKeyboard.js'), /if \(e\.key === 'Tab' && !ctxKeepsTab\(document\.activeElement\)\)/);
  assert.match(src('scriptEditor.js'), /editor\.selectionStart = a \+ 2;/);
});

test('the flyout is a ui/ module: no llm, no facade of its own', () => {
  for (const name of ['ctxScript.js', 'ctxScriptItem.js', 'ctxScriptEditor.js', 'scriptEditor.js',
    'scriptHighlight.js']) {
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

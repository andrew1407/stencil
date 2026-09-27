// The context menu's "Stencil Script ▸" entry: an ordinary submenu parent whose FLYOUT is a
// compact twin of the script window, driven through the DOM-lite stubs — building, painting,
// running, gating, engagement and Tab. Its markup is pinned in ctxScript-markup.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import { scriptFlyoutHtml } from '../../../js/ui/ctx/scriptItem.js';
import { wireCtxScript } from '../../../js/ui/ctx/script.js';
import { setScriptText } from '../../../js/ui/script/buffer.js';
import { ctxKeepsTab, wireCtxKeyboard } from '../../../js/ui/ctx/keyboard.js';
import { createStubElement, installDom } from '../../helpers/dom.js';

const FLYOUT = scriptFlyoutHtml();

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
test('the flyout counts as engaged while a caret is in it or a script runs', async (t) => {
  const engaged = [];
  let flyout;
  const { doc, item, host, restore } = rig({ stencil: { crop: () => { engaged.push(flyout._keepOpen()); } } });
  t.after(restore);
  flyout = doc.getElementById('ctx-script-sub');
  const editor = flyout.appendChild(doc.getElementById('ctx-script-editor'));
  assert.equal(flyout._keepOpen(), false, 'idle, with the caret elsewhere');
  doc.activeElement = editor;
  assert.equal(flyout._keepOpen(), true, 'the hover-out timers must not yank the editor away mid-script');
  doc.activeElement = null;
  typed(doc, '@crop 10%');
  doc.getElementById('ctx-script-run').dispatch('click');
  await settle();
  assert.deepEqual(engaged, [true], 'engaged while the script runs');
  assert.equal(flyout._keepOpen(), false, 'and released after');
  // Everything the flyout offers leaves the menu open; only the touch hand-over closes it.
  item.dispatch('click', { target: item });
  flyout.dispatch('click');
  doc.getElementById('ctx-script-download').dispatch('click');
  doc.getElementById('ctx-script-clear').dispatch('click');
  doc.getElementById('ctx-script-upload').dispatch('change', { target: { value: 'x', files: [{ name: 'a.stc', text: async () => '@save' }] } });
  await settle();
  assert.equal(doc.getElementById('ctx-script-editor').value, '@save');
  assert.equal(host.closes, 0, 'the touch hand-over to the window is the ONLY thing that closes the menu');
});

test('the editor keeps Tab for itself — it indents, it does not leave', (t) => {
  assert.match(FLYOUT, /data-ctx-keep-tab="1"/);
  assert.ok(ctxKeepsTab({ dataset: { ctxKeepTab: '1' } }));
  assert.ok(!ctxKeepsTab({ dataset: {} }), 'every other control lets Tab walk the flyout');
  const { doc, restore } = rig();
  t.after(restore);
  const editor = doc.getElementById('ctx-script-editor');
  editor.dataset.ctxKeepTab = '1';
  const run = doc.getElementById('ctx-script-run');
  for (const c of [editor, run]) c.getClientRects = () => [{}];
  const sub = createStubElement('div', { querySelectorAll: () => [editor, run], contains: (n) => n === editor || n === run });
  sub.classList.add('ctx-sub-visible');
  wireCtxKeyboard({ menu: createStubElement('div'), menuIsOpen: () => true, chatRowMenuOpen: () => false,
    closeSub() {}, positionSub() {}, activeSub: () => sub, setActiveSub() {} });
  const tab = (target) => {
    const ev = { key: 'Tab', target, prevented: false, preventDefault() { ev.prevented = true; }, stopPropagation() {} };
    doc.activeElement = target;
    doc.dispatch('keydown', ev);
    return ev.prevented;
  };
  assert.equal(tab(run), true, 'a plain control hands Tab to the flyout walk');
  assert.equal(editor.focused, true, 'which wraps round to the editor');
  run.focused = editor.focused = false;
  assert.equal(tab(editor), false, 'the editor keeps it');
  assert.equal(run.focused, false, 'focus stays put');
  Object.assign(editor, { value: 'ab', selectionStart: 1, selectionEnd: 1 });
  editor.dispatch('keydown', { key: 'Tab', preventDefault() {} });
  assert.deepEqual([editor.value, editor.selectionStart, editor.selectionEnd], ['a  b', 3, 3], 'it indents');
});

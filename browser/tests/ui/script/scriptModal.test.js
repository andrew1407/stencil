// The script window's shape: where its opener sits in the toolbar, what the window is made
// of, and that the highlight layer is built from nodes rather than markup. Modelled on
// metaModals.test.js.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { installDom, createStubElement } from '../../helpers/dom.js';
import { hotkeyActions } from '../../../js/ui/bindings/keys/hotkeyActions.js';
import { paintInto } from '../../../js/ui/script/highlight.js';
import { wireScriptEditor } from '../../../js/ui/script/editor.js';
import { wireDropPaste } from '../../../js/ui/bindings/dropPaste.js';
import { StencilDropOverlay } from '../../../js/ui/canvas/dropOverlay.js';

import { layout } from '../../../js/ui/layout.js';
import { HOTKEYS_WHILE_TYPING } from '../../../js/ui/bindings/keys/hotkeyRules.js';
import hotkeysConfig from '../../../../common/config/hotkeysConfig.json' with { type: 'json' };
import uiStrings from '../../../../common/config/uiStrings.json' with { type: 'json' };

const MARKUP = layout();
const src = (p) => readFileSync(fileURLToPath(new URL(p, import.meta.url)), 'utf8');

test('the opener leads the Data section', () => {
  const data = MARKUP.slice(MARKUP.indexOf('<div class="ctrl-section-label">Data</div>'));
  const section = data.slice(0, data.indexOf('</div>\n            </div>'));
  const order = [...section.matchAll(/id="([a-z-]+)"/g)].map((m) => m[1]);
  assert.deepEqual(order, [
    'script-btn', 'copy-json-btn', 'download-json', 'upload-json', 'upload-json-btn', 'clear-storage',
  ]);
});

test('the opener names its hotkey and its tooltip, and carries no disabled reason', () => {
  const btn = MARKUP.slice(MARKUP.indexOf('id="script-btn"'));
  assert.match(btn.slice(0, 300), /data-hk-title="openScript"/);
  assert.match(btn.slice(0, 300), /data-title="Stencil script \(\.stc\)/);
  // The window opens with no image — a script can bring its own @source — so nothing
  // disables this control and a reason line would never be shown.
  assert.doesNotMatch(btn.slice(0, 300), /data-disabled-reason/);
});

test('the window is composed with its editor, its diagnostics strip and its actions', () => {
  for (const id of ['script-overlay', 'script-close', 'script-editor-wrap', 'script-highlight',
    'script-editor', 'script-diag', 'script-upload', 'script-upload-btn', 'script-copy',
    'script-download', 'script-clear', 'script-run']) {
    assert.equal(MARKUP.split(`id="${id}"`).length - 1, 1, `${id} appears exactly once`);
  }
  // Run leads the row as the primary action; Clear throws work away, so it wears the danger red at the far end.
  // Sliced to the actions bar, which sits ABOVE the editor, or the editor's own ids sweep in with it.
  const bar = MARKUP.slice(MARKUP.indexOf('class="script-actions-bar"'));
  const footer = bar.slice(0, bar.indexOf('class="settings-body"'));
  const order = [...footer.matchAll(/id="(script-[a-z-]+)"/g)].map((m) => m[1]);
  assert.deepEqual(order,
    ['script-run', 'script-copy', 'script-download', 'script-upload', 'script-upload-btn', 'script-clear']);
  assert.match(footer, /id="script-run" class="btn-icon-text primary"/);
  assert.match(footer, /id="script-clear" class="btn-icon-text danger"/);
  assert.match(footer, /id="script-clear"[^>]*>.*ic-trash/);
});

test('the file input accepts .stc only', () => {
  assert.match(MARKUP, /id="script-upload" accept="\.stc"/);
});

test('the hotkey is registered, openable while typing, and drives the opener', () => {
  const entry = hotkeysConfig.find((h) => h.id === 'openScript');
  assert.ok(entry, 'openScript is in the canonical hotkey registry');
  assert.equal(entry.default, 'Alt+Shift+S');
  assert.ok(HOTKEYS_WHILE_TYPING.includes('openScript'),
    'the window must close from inside its own editor');
  const doc = installDom();
  try {
    const clicks = [];
    const btn = doc.register('script-btn', createStubElement('button', { click: () => clicks.push('script-btn') }));
    const { HK_HANDLERS } = hotkeyActions({});
    HK_HANDLERS.openScript();
    assert.deepEqual(clicks, ['script-btn'], 'the hotkey presses the opener');
    btn.disabled = true;
    HK_HANDLERS.openScript();
    assert.deepEqual(clicks, ['script-btn'], 'and a disabled opener stays shut');
  } finally { doc.restore(); }
});

test('the window is in the openWindow registry, pointing at its own overlay and opener', () => {
  const row = uiStrings.windows.find((w) => w.key === 'script');
  assert.ok(row, 'stencil.openWindow("script") has a row');
  assert.equal(row.overlay, 'script-overlay');
  assert.equal(row.opener, 'script-btn');
  assert.equal(row.hotkey, 'openScript');
});

test('the highlight layer is built from nodes, never from markup', () => {
  const markup = [];
  const noMarkup = (el) => Object.defineProperty(el, 'innerHTML', { get: () => '', set: (v) => markup.push(v) });
  const doc = installDom({ createElement: (tag) => noMarkup(createStubElement(tag)) });
  try {
    const pre = noMarkup(createStubElement('pre'));
    const text = '# <img src=x onerror=alert(1)>\n@crop 10%';
    paintInto(pre, text, false);
    assert.deepEqual(markup, [], 'a script is untrusted text — it never becomes markup');
    const spans = pre.childNodes.filter((n) => n.tagName === 'SPAN');
    assert.ok(spans.length > 0 && spans.every((n) => n.className.startsWith('stk-')), 'tokens are spans');
    assert.equal(pre.childNodes.map((n) => n.textContent).join(''), text, 'set as text, character for character');
  } finally { doc.restore(); }
});

test('the window and the context-menu flyout paint through the ONE highlighter', () => {
  // Two copies of a token painter — or of the editor wiring around it — would let the two
  // editors disagree about a line.
  assert.match(src('../../../js/ui/script/editor.js'),
    /import \{ paintInto, showDiagnostic \} from '[^']*highlight\.js'/);
  for (const module of ['script/modal.js', 'ctx/scriptEditor.js']) {
    assert.match(src(`../../../js/ui/${module}`),
      /import \{ wireScriptEditor \} from '[^']*editor\.js'/, module);
    assert.ok(!/paintInto|showDiagnostic/.test(src(`../../../js/ui/${module}`)),
      `${module} wires the shared editor instead of painting for itself`);
  }
});

test('a dropped .stc is routed to the one loader, and the overlay says so', async () => {
  const toasts = [];
  const doc = installDom({}, { window: { innerWidth: 1000 } });
  try {
    doc.register('global-drop-overlay', createStubElement('div'));
    doc.register('notify-balloon', createStubElement('div', { notify: (msg) => toasts.push(msg) }));
    doc.register('script-overlay', createStubElement('div')).classList.add('modal-open');
    const inputs = [];
    const editor = doc.register('script-editor', createStubElement('textarea', { dispatchEvent: (e) => inputs.push(e.type) }));
    wireDropPaste({ image: null });
    const file = { name: 'crop.stc', type: '', text: async () => '@crop 5%' };
    doc.dispatch('drop', { preventDefault() {}, clientX: 10, clientY: 10, dataTransfer: { types: ['Files'], files: [file] } });
    await new Promise((r) => setTimeout(r, 0));
    assert.equal(editor.value, '@crop 5%', 'the open window takes the dropped script');
    assert.deepEqual(inputs, ['input'], 'as if typed, so the buffer and the paint follow');
    assert.deepEqual(toasts, ['Loaded crop.stc into the script window']);
  } finally { doc.restore(); }
  assert.match(StencilDropOverlay.inner(), /\.stc script/);
});

test('Run is gated on the script having something to do, and Ctrl+Enter obeys the same gate', async () => {
  const doc = installDom();
  try {
    doc.createTextNode = (text) => ({ nodeType: 3, textContent: text });
    const ids = { run: 'g-run', copy: 'g-copy', download: 'g-download', clear: 'g-clear' };
    for (const id of Object.values(ids)) doc.register(id, createStubElement('button'));
    const editor = createStubElement('textarea');
    const runs = [];
    let busy = false;
    const { dispose } = wireScriptEditor({ editor, pre: createStubElement('pre'), strip: createStubElement('div'), ids,
      app: {}, onRun: async (t) => { runs.push(t); }, onUpload: () => {}, busy: () => busy });
    const gates = (text) => {
      editor.value = text;
      editor.dispatch('input');
      return Object.values(ids).map((id) => doc.getElementById(id).disabled);
    };
    const ctrlEnter = () => editor.dispatch('keydown', { key: 'Enter', ctrlKey: true, preventDefault() {} });
    // [run, copy, download, clear]: a comment-only script lowers to no ops — running it did nothing.
    assert.deepEqual(gates(''), [true, true, true, true]);
    assert.deepEqual(gates('# just a note'), [true, false, false, false], 'Copy, Download and Clear only need text');
    ctrlEnter();
    assert.deepEqual(runs, [], 'Ctrl+Enter obeys the disabled Run');
    assert.deepEqual(gates('@nope 5'), [false, false, false, false], 'an errored script keeps Run live');
    ctrlEnter();
    await new Promise((r) => setTimeout(r, 0));
    busy = true;
    ctrlEnter();
    assert.deepEqual(runs, ['@nope 5'], 'one run; a busy surface refuses the second');
    dispose();
  } finally { doc.restore(); }
});

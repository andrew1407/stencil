// The script window's shape: where its opener sits in the toolbar, what the window is made
// of, and that the highlight layer is built from nodes rather than markup. Modelled on
// metaModals.test.js.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { layout } from '../js/ui/layout.js';
import { HOTKEYS_WHILE_TYPING } from '../js/ui/bindings/hotkeyRules.js';
import hotkeysConfig from '../js/config/hotkeysConfig.json' with { type: 'json' };
import uiStrings from '../js/config/uiStrings.json' with { type: 'json' };

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

test('the opener names its hotkey, its tooltip and why it can be disabled', () => {
  const btn = MARKUP.slice(MARKUP.indexOf('id="script-btn"'));
  assert.match(btn.slice(0, 300), /data-hk-title="openScript"/);
  assert.match(btn.slice(0, 300), /data-title="Stencil script \(\.stc\)/);
  assert.match(btn.slice(0, 300), /data-disabled-reason="Open an image first"/);
});

test('the window is composed with its editor, its diagnostics strip and three actions', () => {
  for (const id of ['script-overlay', 'script-close', 'script-editor-wrap', 'script-highlight',
    'script-editor', 'script-diag', 'script-upload', 'script-upload-btn', 'script-copy',
    'script-download', 'script-run']) {
    assert.equal(MARKUP.split(`id="${id}"`).length - 1, 1, `${id} appears exactly once`);
  }
  // Run is the primary action and comes last, the way Save does in every other window.
  // From the first footer control on, the only script-* ids left are the footer's own.
  const footer = MARKUP.slice(MARKUP.indexOf('id="script-copy"'));
  const order = [...footer.matchAll(/id="(script-[a-z-]+)"/g)].map((m) => m[1]);
  assert.deepEqual(order,
    ['script-copy', 'script-download', 'script-upload', 'script-upload-btn', 'script-run']);
  assert.match(footer, /id="script-run" class="btn-icon-text primary"/);
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
  assert.match(src('../js/ui/bindings/hotkeyActions.js'), /openScript: \(\) => clickIfActive\('script-btn'\)/);
});

test('the window is in the openWindow registry, pointing at its own overlay and opener', () => {
  const row = uiStrings.windows.find((w) => w.key === 'script');
  assert.ok(row, 'stencil.openWindow("script") has a row');
  assert.equal(row.overlay, 'script-overlay');
  assert.equal(row.opener, 'script-btn');
  assert.equal(row.hotkey, 'openScript');
});

test('the highlight layer is built from nodes, never from markup', () => {
  const code = src('../js/ui/scriptHighlight.js');
  assert.ok(!/innerHTML/.test(code), 'a script is untrusted text — it never becomes markup');
  assert.match(code, /createElement\('span'\)/);
  assert.match(code, /textContent = /);
});

test('the window and the context-menu flyout paint through the ONE highlighter', () => {
  // Two copies of a token painter would let the two editors disagree about a line.
  for (const module of ['scriptModal.js', 'ctxScriptEditor.js']) {
    assert.match(src(`../js/ui/${module}`),
      /import \{ paintInto, showDiagnostic \} from '\.\/scriptHighlight\.js'/, module);
    assert.ok(!/const paintInto|function paintInto/.test(src(`../js/ui/${module}`)),
      `${module} imports the painter instead of keeping its own`);
  }
});

test('a dropped .stc is routed to the one loader, and the overlay says so', () => {
  const drop = src('../js/ui/bindings/dropPaste.js');
  assert.match(drop, /endsWith\('\.stc'\)/);
  assert.match(drop, /loadScriptFile\(file, \{ app \}\)/);
  assert.match(src('../js/ui/dropOverlay.js'), /\.stc script/);
});

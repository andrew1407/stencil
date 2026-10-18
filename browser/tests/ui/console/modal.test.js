// The console window (js/ui/console/modal.js): composed once with its ids, the console icon ahead
// of the script one, and a stylesheet that never dims the page. The aside shell it rides is
// tests/ui/modal/asideShell.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { layout } from '../../../js/ui/layout.js';
import { consoleModalInner } from '../../../js/ui/console/markup.js';

const MARKUP = layout();
const count = (needle) => MARKUP.split(needle).length - 1;

test('the window is composed once, its ids once, the console icon before the script one', () => {
  assert.equal(count('<stencil-console-modal'), 1);
  for (const id of ['console-overlay', 'console-close', 'console-out', 'console-input', 'console-upload', 'console-btn']) assert.equal(count(`id="${id}"`), 1, id);
  assert.ok(MARKUP.indexOf('id="console-btn"') < MARKUP.indexOf('id="script-btn"'));
  assert.match(consoleModalInner(), /data-min-w="420" data-min-h="240"/);
  assert.match(consoleModalInner(), /accept="[^"]*\.stencil"/, 'a bare /upload takes a project file too');
});

test('the stylesheet keeps the overlay transparent and pointer-free under every backdrop setting', () => {
  const css = readFileSync(new URL('../../../css/components/console/window.css', import.meta.url), 'utf8');
  assert.match(css, /#console-overlay\.modal-open,[^{]*#console-overlay\.modal-closing \{[^}]*background: transparent/);
  assert.match(css, /pointer-events: none/);
  assert.match(css, /#console-overlay \.app-modal \{ pointer-events: auto; \}/);
  assert.doesNotMatch(css, /backdrop-filter: blur/);
});

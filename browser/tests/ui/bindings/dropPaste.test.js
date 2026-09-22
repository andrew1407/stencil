// The drop handler's failure notice is read from source: both drop paths must surface the
// module's message (which names the host and the count) and carry the same fallback hint.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const JS = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../js');
const read = (rel) => fs.readFileSync(path.join(JS, rel), 'utf8');

const HINT = 'If the site blocks cross-origin downloads, try the extension or desktop app.';

test('the drop notice surfaces the fetch error rather than blaming CORS itself', () => {
  const src = read('ui/bindings/dropPaste.js');
  assert.match(src, /Could not load the dragged image — \$\{err\.message\}/);
  assert.ok(src.includes(HINT), 'the extension/desktop hint stays');
  assert.ok(!/blocked \(CORS or an unreachable host\)/.test(src),
    'the generic CORS wording belongs to dragImageUrl.js, which names the host');
});

test('the chat-attach path words its failure the same way', () => {
  const src = read('ui/chat/panel.js');
  assert.match(src, /Couldn't attach that image — \$\{err\.message\}/);
  assert.ok(src.includes(HINT), 'both drop paths offer the same way out');
});

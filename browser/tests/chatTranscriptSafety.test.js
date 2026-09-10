// Invariant: the chat transcript renders MODEL text as data. js/ui/chatView.js writes it
// into the row's one text node with textContent; a markdown renderer dropped in behind
// innerHTML would hand every reply a DOM-injection primitive, so it is pinned here.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const SRC = readFileSync(new URL('../js/ui/chatView.js', import.meta.url), 'utf8');
const INNER_HTML = [...SRC.matchAll(/[\w.$]+\.innerHTML\s*=\s*([^;]+);/g)].map((m) => m[1].trim());

test('the model reply lands in the row text node via textContent', () => {
  assert.match(SRC, /const textEl = rowTextNode\(el\);/);
  assert.match(SRC, /textEl\.textContent = row\.text;/);
});

test('no model/log string ever reaches innerHTML', () => {
  assert.ok(INNER_HTML.length, 'the innerHTML scan matched nothing — has the file moved?');
  for (const rhs of INNER_HTML) {
    assert.doesNotMatch(rhs, /\brow\b|\btext\b|\bmsg\b|\bcontent\b|\bplan\b/, `innerHTML = ${rhs}`);
  }
});

test('every innerHTML value is a built-in icon or literal, escaping what it interpolates', () => {
  const CALLABLE = new Set(['escapeHtml', 'icon']);
  for (const rhs of INNER_HTML) {
    for (const [, token, next] of rhs.matchAll(/\$\{\s*([A-Za-z_$][\w.$]*)\s*([(?]?)/g)) {
      if (next === '?') continue;   // a ternary TEST is never itself rendered
      assert.equal(next, '(', `innerHTML interpolates \${${token}} raw — wrap it in escapeHtml()`);
      assert.ok(CALLABLE.has(token), `innerHTML interpolates ${token}() — escape it`);
    }
  }
});

test('the transcript parses no HTML by any other route', () => {
  for (const sink of ['insertAdjacentHTML', 'outerHTML =', 'createContextualFragment', 'document.write', 'DOMParser']) {
    assert.ok(!SRC.includes(sink), `${sink} in chatView.js`);
  }
});

// The .stc paint pass both editors share. The lexer classifies on the '@' alone — every word
// after one arrives as a `directive` token — so the colour is only meaningful if the painter
// checks the word against the canonical list first.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import { DIRECTIVES } from '../js/core/scriptTypes.js';
import { paintInto } from '../js/ui/scriptHighlight.js';
import { createStubElement, installDom } from './helpers/dom.js';

const doc = installDom({});
doc.createTextNode = (text) => ({ nodeType: 3, textContent: text });

const paint = (text, withDiagnostics = false) => {
  const pre = createStubElement('pre');
  pre.nodes = [];
  Object.defineProperty(pre, 'firstChild', { get: () => pre.nodes[0] ?? null });
  pre.appendChild = (n) => { pre.nodes.push(n); return n; };
  pre.removeChild = (n) => { pre.nodes.splice(pre.nodes.indexOf(n), 1); return n; };
  paintInto(pre, text, withDiagnostics);
  return pre.nodes.map((n) => ({ cls: n.className || '', text: n.textContent }));
};
const classOf = (text, word) => paint(text).find((n) => n.text === word)?.cls ?? '';

test('a real directive is painted as one', () => {
  assert.equal(classOf('@crop 10%', '@crop'), 'stk-directive');
  for (const word of DIRECTIVES) {
    assert.equal(classOf(`@${word}`, `@${word}`), 'stk-directive', `@${word}`);
  }
});

test('case does not cost a directive its colour — the lowering lowercases it too', () => {
  assert.equal(classOf('@CROP 10%', '@CROP'), 'stk-directive');
  assert.equal(classOf('@Crop 10%', '@Crop'), 'stk-directive');
});

test('a word that is not a directive is ordinary text, whatever the sigil says', () => {
  const nodes = paint('@nonsense 5');
  assert.ok(!nodes.some((n) => n.cls.includes('stk-directive')), 'nothing is coloured as valid');
  assert.ok(nodes.some((n) => n.cls === '' && n.text.startsWith('@nonsense')),
    'it lands in a plain text node');
  assert.ok(nodes.some((n) => n.cls === 'stk-number' && n.text === '5'),
    'and the rest of the line is still lexed');
});

test('a template parameter is untouched — it was never a directive token', () => {
  assert.equal(classOf('@1', '@1'), 'stk-param');
  assert.equal(classOf('@12', '@12'), 'stk-param');
});

test('directives inside a @stencil template body still highlight', () => {
  const nodes = paint('@stencil box:\n  @rect 0,0 @1,@2\n');
  const classes = nodes.filter((n) => n.cls).map((n) => n.cls);
  assert.equal(classes.filter((c) => c === 'stk-directive').length, 2, '@stencil and @rect');
  assert.ok(classes.includes('stk-param'), 'and the body\'s parameters are still parameters');
});

test('an unknown directive still earns its underline once the script has been RUN', () => {
  const marked = paint('@nonsense 5', true).filter((n) => n.cls.includes('stk-error'));
  assert.equal(marked.length, 1, 'the diagnostic paints its own mark where the token was');
  assert.equal(marked[0].text, '@nonsense');
  assert.ok(!marked[0].cls.includes('stk-directive'), 'underlined, never coloured as valid');
});

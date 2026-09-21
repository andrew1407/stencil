// The .stc paint pass both editors share. The lexer classifies on the '@' alone — every word
// after one arrives as a `directive` token — so the colour is only meaningful if the painter
// checks the word against the canonical list first.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import { DIRECTIVES } from '../js/core/scriptTypes.js';
import { paintInto } from '../js/ui/script/scriptHighlight.js';
import { createStubElement, installDom } from './helpers/dom.js';

const doc = installDom({});
doc.createTextNode = (text) => ({ nodeType: 3, textContent: text });

const stubPre = () => createStubElement('pre');

const streamOf = (pre) => pre.childNodes.map((n) => ({ cls: n.className || '', text: n.textContent }));

const paint = (text, withDiagnostics = false) => {
  const pre = stubPre();
  paintInto(pre, text, withDiagnostics);
  return streamOf(pre);
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

/* The paint patches the nodes already there instead of rebuilding them, so what a <pre>
 * holds must not depend on what it held before: every repaint has to converge on the stream
 * a first paint of that same text lands. These two cases are the proof of that. */
const EDITS = [
  ['', '@crop 10%'],
  ['@crop 10%', ''],
  ['@crop 10%', '@crop 10% 20%'],
  ['@crop 10% 20%', '@crop 10%'],
  ['@line 1px 1px\n@rect 2px 2px', '@line 1px 1px\n@rect 2px 2px\n@save out.png'],
  ['@line 1px 1px\n@rect 2px 2px\n@save out.png', '@line 1px 1px'],
  ['@crop 10%', '# crop 10%'],
  ['# crop 10%', '@crop 10%'],
  ['@stencil box:\n  @rect 0,0 @1,@2\n@use stencil box 5px 5px', '@nonsense'],
  ['@crop 10%', '@crop 10%'],
];

test('a repainted <pre> lands the same stream as a first paint of that text', () => {
  for (const [before, after] of EDITS) {
    for (const withDiagnostics of [false, true]) {
      const patched = stubPre();
      paintInto(patched, before, withDiagnostics);
      paintInto(patched, after, withDiagnostics);
      assert.deepEqual(streamOf(patched), paint(after, withDiagnostics),
        `${JSON.stringify(before)} -> ${JSON.stringify(after)}`);
    }
  }
});

test('a keystroke leaves the untouched lines\' own nodes in place', () => {
  const lines = Array.from({ length: 40 }, (_, i) => `@line ${i}px ${i}px`);
  const pre = stubPre();
  paintInto(pre, lines.join('\n'), false);
  const before = [...pre.childNodes];

  lines[20] = '@line 999px 20px';
  paintInto(pre, lines.join('\n'), false);

  const kept = pre.childNodes.filter((n, i) => before[i] === n).length;
  assert.equal(pre.childNodes.length, before.length, 'the stream is the same shape');
  assert.ok(kept > before.length * 0.9,
    `only the edited line is rebuilt, kept ${kept} of ${before.length}`);
});

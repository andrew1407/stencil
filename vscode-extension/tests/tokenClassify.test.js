// The colour a word gets depends on the directive it sits under, which the lexer does not
// record. These run real buffers through the parser copies and read the classification back.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

import { parseScript } from '../src/parser/index.js';

const { classify } = createRequire(import.meta.url)('../src/lib/tokenClassify.js');

/* Every token of `text` as `text→type`, punctuation dropped: what a reader would see. */
const painted = (text) => {
  const { tokens } = parseScript(text);
  return classify(tokens)
    .map((type, i) => [tokens[i], type])
    .filter(([token]) => token.kind !== 'punct')
    .map(([token, type]) => `${token.text}\u2192${type}`);
};

const typeOf = (text, word) => {
  const hit = painted(text).find((row) => row.startsWith(`${word}\u2192`));
  const type = hit?.split('\u2192')[1];
  return type === 'undefined' ? undefined : type;
};

test('the four directive families each get their own colour', () => {
  const text = '@source a.png:\n    @crop 5%\n    @save out/\n@stencil box:\n    @filter bw\n';
  assert.equal(typeOf(text, '@source'), 'namespace');
  assert.equal(typeOf(text, '@stencil'), 'class');
  assert.equal(typeOf(text, '@crop'), 'keyword');
  assert.equal(typeOf(text, '@filter'), 'keyword');
  assert.equal(typeOf(text, '@save'), 'function');
});

test('@undo and @redo travel with @save, not with the edits', () => {
  const text = '@source a.png:\n    @filter bw\n    @undo\n    @redo 2\n';
  assert.equal(typeOf(text, '@undo'), 'function');
  assert.equal(typeOf(text, '@redo'), 'function');
});

test('a template name is a type, where it is defined and where it is used', () => {
  assert.deepEqual(painted('@stencil box frame:\n'), ['@stencil\u2192class', 'box\u2192type', 'frame\u2192type']);
  const call = painted('@source a.png:\n    @use stencil box frame:\n');
  assert.ok(call.includes('stencil\u2192keyword'), 'the sub-keyword stays a keyword');
  assert.ok(call.includes('box\u2192type') && call.includes('frame\u2192type'), call.join(' '));
});

test('a filter mode, a line style and a crop key are each their own thing', () => {
  assert.equal(typeOf('@source a.png:\n    @filter sepia\n', 'sepia'), 'enumMember');
  assert.equal(typeOf('@use line dashed\n', 'dashed'), 'modifier');
  assert.equal(typeOf('@use line fill red\n', 'fill'), 'modifier');
  assert.equal(typeOf('@source a.png:\n    @crop x1=10% aspect=3:2\n', 'x1'), 'variable');
  assert.equal(typeOf('@source a.png:\n    @crop x1=10% aspect=3:2\n', 'aspect'), 'variable');
});

test('a colour name colours like a hex colour, wherever it stands', () => {
  assert.equal(typeOf('@use line red\n', 'red'), 'property');
  assert.equal(typeOf('@use line fill transparent\n', 'transparent'), 'modifier');
  assert.equal(typeOf('@source a.png:\n    @filter #7c3aed\n', '#7c3aed'), 'property');
});

test('the thing a directive names outside the file reads as a string', () => {
  assert.equal(typeOf('@source shots/*.png:\n', 'shots/*.png'), 'string');
  assert.equal(typeOf('@source https://x.test/a.png:\n', 'https://x.test/a.png'), 'string');
  assert.equal(typeOf('@source a.png:\n    @save reviewed/\n', 'reviewed/'), 'string');
  assert.equal(typeOf('@source a.png:\n    @layout grid.json replace\n', 'grid.json'), 'string');
  assert.equal(typeOf('@source a.png:\n    @layout grid.json replace\n', 'replace'), 'modifier');
});

test('@use takes its meaning from the word after it', () => {
  assert.equal(typeOf('@use cm\n', 'cm'), 'operator');
  assert.equal(typeOf('@use line red\n', 'line'), 'keyword');
  assert.equal(typeOf('@source a.png:\n    @use stencil box:\n', 'stencil'), 'keyword');
});

test('a statement ends at a newline, so the next line starts clean', () => {
  // `bw` is a filter mode under @filter and nothing at all on a line of its own.
  assert.equal(typeOf('@source a.png:\n    @filter bw\n', 'bw'), 'enumMember');
  assert.equal(typeOf('@source a.png:\n    @filter none\n    @crop bw\n', 'bw'), undefined);
});

test('a parameter, a number, a unit and a comment keep the kinds the lexer gave them', () => {
  const text = '@stencil s:\n    @use line @1, 3px   # note\n';
  assert.equal(typeOf(text, '@1'), 'parameter');
  assert.equal(typeOf(text, '3'), 'number');
  assert.equal(typeOf(text, 'px'), 'operator');
  assert.ok(painted(text).some((row) => row.endsWith('\u2192comment')), painted(text).join(' '));
});

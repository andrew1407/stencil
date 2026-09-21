// The vocabulary is data the editor reads back at the user, so it is held to the parser's
// own word lists — a directive the engine knows with nothing to say about it is a hole.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

import { DIRECTIVES as PARSER_DIRECTIVES, isUnitWord } from '../src/parser/script/scriptTypes.js';

const vocabulary = createRequire(import.meta.url)('../src/lib/vocabulary.js');

test('every directive the parser knows is documented, and no other', () => {
  assert.deepEqual([...vocabulary.DIRECTIVE_NAMES].sort(), [...PARSER_DIRECTIVES].sort());
});

test('every documented unit is one the parser accepts', () => {
  for (const unit of vocabulary.UNIT_NAMES) assert.ok(isUnitWord(unit), `${unit} is not a unit`);
  assert.equal(vocabulary.UNIT_NAMES.length, 5, 'px, cm, mm, in, %');
});

test('every directive carries a family, a signature, a summary and an example', () => {
  const families = new Set(['source', 'template', 'edit', 'output']);
  for (const [name, entry] of Object.entries(vocabulary.DIRECTIVES)) {
    assert.ok(families.has(entry.group), `@${name} has no known family`);
    assert.match(entry.signature, /^@/, `@${name} has no signature`);
    assert.match(entry.summary, /\.$/, `@${name}'s summary is not a sentence`);
    assert.ok(entry.example.includes('@'), `@${name} has no example`);
  }
});

test('the word groups are the ones the classifier and the completions ask for', () => {
  assert.deepEqual([...vocabulary.MODES], ['bw', 'sepia', 'invert', 'contour', 'none']);
  assert.deepEqual([...vocabulary.CROP_KEYS], ['x1', 'x2', 'y1', 'y2', 'aspect']);
  for (const style of ['solid', 'dashed', 'dotted', 'fill', 'point']) {
    assert.ok(vocabulary.STYLES.includes(style), `${style} is missing`);
  }
});

test('explain renders a directive as Markdown, with its signature and example fenced', () => {
  const markdown = vocabulary.explain('@crop');
  assert.match(markdown, /^\*\*@crop\*\* — /);
  assert.equal(markdown.match(/```stc/g).length, 2, 'the signature and the example are fenced');
  assert.equal(vocabulary.explain('crop'), markdown, 'the @ is optional');
});

test('explain is case-insensitive and empty for a word it does not know', () => {
  assert.equal(vocabulary.explain('@CROP'), vocabulary.explain('@crop'));
  assert.equal(vocabulary.explain('SEPIA'), vocabulary.explain('sepia'));
  assert.equal(vocabulary.explain('frobnicate'), '');
  assert.equal(vocabulary.explain('@frobnicate'), '');
});

test('groupOf names the colour family, and nothing for an unknown word', () => {
  assert.equal(vocabulary.groupOf('source'), 'source');
  assert.equal(vocabulary.groupOf('SAVE'), 'output');
  assert.equal(vocabulary.groupOf('rect'), 'edit');
  assert.equal(vocabulary.groupOf('nope'), undefined);
});

// The heading already reads `**fill** — `; a second dash makes the tooltip say it twice.
test('a summary states the meaning, and leaves the heading its dash', () => {
  const entries = { ...vocabulary.DIRECTIVES, ...vocabulary.WORDS, ...vocabulary.UNITS };
  for (const [word, entry] of Object.entries(entries)) {
    assert.ok(!entry.summary.includes('—'),
      `${word}: its summary carries a dash, so the tooltip reads "${word} — ${entry.summary}"`);
    assert.match(entry.summary, /^[A-Z@`]/, `${word}: a summary reads as a sentence`);
  }
});

// The TextMate grammar and the language configuration are data VS Code compiles at load
// time, so a typo is invisible until a user opens a .stc. This compiles every regex here.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { DIRECTIVES } from '../src/parser/script/types.js';

const read = (rel) => JSON.parse(readFileSync(new URL(rel, import.meta.url), 'utf8'));

const grammar = read('../syntaxes/stc.tmLanguage.json');
const langConfig = read('../language-configuration.json');
const manifest = read('../package.json');

/* Oniguruma (the grammar's engine) accepts a leading inline `(?i)`; JS takes the flag
 * instead. Nothing else in this grammar is outside the JS dialect. */
const toJsRegex = (pattern) => (pattern.startsWith('(?i)')
  ? new RegExp(pattern.slice(4), 'i')
  : new RegExp(pattern));

/* Every rule in the grammar, top-level patterns and repository alike. */
const rules = (node, out = []) => {
  if (Array.isArray(node)) { for (const item of node) rules(item, out); return out; }
  if (!node || typeof node !== 'object') return out;
  if (node.match || node.begin || node.end || node.name) out.push(node);
  if (node.patterns) rules(node.patterns, out);
  // captures and the repository are keyed objects, not arrays.
  for (const key of ['captures', 'beginCaptures', 'endCaptures', 'repository']) {
    if (node[key]) for (const value of Object.values(node[key])) rules(value, out);
  }
  return out;
};

// The root node carries the grammar's display name, not a scope; the walk starts below it.
const ALL = rules({ patterns: grammar.patterns, repository: grammar.repository });

test('the grammar declares the contributed language, scope and path', () => {
  const [contributed] = manifest.contributes.grammars;
  assert.equal(grammar.scopeName, contributed.scopeName);
  assert.equal(contributed.scopeName, 'source.stc');
  assert.equal(contributed.language, manifest.contributes.languages[0].id);
  assert.equal(contributed.path, './syntaxes/stc.tmLanguage.json');
  assert.equal(manifest.contributes.languages[0].configuration, './language-configuration.json');
});

test('every grammar regex compiles', () => {
  let compiled = 0;
  for (const rule of ALL) {
    for (const key of ['match', 'begin', 'end']) {
      if (!rule[key]) continue;
      assert.doesNotThrow(() => toJsRegex(rule[key]), `${key}: ${rule[key]}`);
      compiled += 1;
    }
  }
  assert.ok(compiled >= 9, `only ${compiled} patterns were compiled — the walk missed rules`);
});

test('every include resolves to a repository entry', () => {
  const includes = JSON.stringify(grammar).match(/"#[a-z-]+"/g) ?? [];
  assert.ok(includes.length >= 9, 'the top-level patterns list every rule');
  for (const raw of includes) {
    const name = raw.slice(2, -1);
    assert.ok(grammar.repository[name], `#${name} has no repository entry`);
  }
});

test('every repository entry is reachable from the top-level patterns', () => {
  const reached = new Set(grammar.patterns.map((p) => p.include?.slice(1)));
  assert.deepEqual([...Object.keys(grammar.repository)].filter((k) => !reached.has(k)), []);
});

test('every scope name ends in .stc', () => {
  const names = ALL.map((r) => r.name).filter(Boolean);
  assert.ok(names.length >= 9, `only ${names.length} scope names were seen`);
  for (const name of names) assert.match(name, /\.stc$/, `${name} is not scoped to this language`);
});

test('the scopes the contract asks for are all present', () => {
  const names = new Set(ALL.map((r) => r.name).filter(Boolean));
  for (const prefix of [
    'comment.line', 'keyword.control.directive', 'entity.name.function', 'constant.numeric',
    'keyword.other.unit', 'constant.other.color', 'string.quoted.double', 'variable.parameter',
    'punctuation',
  ]) {
    assert.ok([...names].some((n) => n.startsWith(prefix)), `no scope starts with ${prefix}`);
  }
});

test('the directive rule lists exactly the directives the parser knows', () => {
  const [, body] = /@\(\?:([a-z|]+)\)/.exec(grammar.repository.directive.match);
  assert.deepEqual(body.split('|').sort(), [...DIRECTIVES].sort());
});

test('the colour rule wins over the comment rule for a hex word', () => {
  const color = toJsRegex(grammar.repository.color.match);
  const comment = toJsRegex(grammar.repository.comment.match);
  for (const hex of ['#ccc', '#cccc', '#00ff00', '#00ff0080']) {
    assert.match(hex, color, `${hex} is a colour`);
    assert.equal(comment.test(hex), false, `${hex} must not read as a comment`);
  }
  for (const text of ['# a note', '#ccccc still a note', '#zzz']) {
    assert.match(text, comment, `${text} is a comment`);
  }
});

test('the number rule splits a united length and leaves x1= alone', () => {
  const number = toJsRegex(grammar.repository.number.match);
  assert.deepEqual([...'-10cm'.match(number)].slice(1), ['-10', 'cm']);
  assert.deepEqual([...'7'.match(number)].slice(1), ['7', undefined]);
  assert.equal(number.exec('x1=')?.index, undefined, 'the 1 in x1 is part of a key');
});

test('the template rules name what follows @stencil and @use stencil', () => {
  const def = toJsRegex(grammar.repository['stencil-def'].match);
  assert.equal('@stencil box frame:'.match(def)[2], 'box frame');
  const use = toJsRegex(grammar.repository['use-stencil'].match);
  assert.equal('@use stencil box frame 10'.match(use)[3], 'box frame 10');
});

test('every language-configuration regex compiles under the JS engine VS Code uses', () => {
  const patterns = [
    langConfig.wordPattern,
    langConfig.indentationRules.increaseIndentPattern,
    langConfig.indentationRules.decreaseIndentPattern,
  ];
  for (const pattern of patterns) assert.doesNotThrow(() => new RegExp(pattern), pattern);
  assert.deepEqual(langConfig.comments.lineComment, { comment: '#', noIndent: false });
  const increase = new RegExp(langConfig.indentationRules.increaseIndentPattern);
  assert.match('@source a.png:', increase, 'a block header opens an indent');
  assert.match('@stencil box:   # note', increase, 'a trailing comment does not hide the colon');
  assert.doesNotMatch('@crop 10%', increase);
  assert.match('@SOURCE b.png:', new RegExp(langConfig.indentationRules.decreaseIndentPattern));
  assert.match('@crop', new RegExp(langConfig.wordPattern), 'a directive is one word');
});

/* The marker is a comment to JavaScript and a directive to a reader, so it is coloured by an
 * INJECTION: `// @use stencil` lights up inside a line comment, in a .stcjs and in the plain
 * .js that opted in, without this tree re-spelling one rule of JavaScript. */
test('the `@use stencil` marker is injected into JavaScript and Python line comments', () => {
  const contributed = manifest.contributes.grammars.find((g) => g.injectTo);
  assert.ok(contributed, 'no injection grammar is contributed');
  assert.deepEqual(contributed.injectTo, ['source.js', 'source.stcjs', 'source.python', 'source.pystc']);
  assert.equal(contributed.language, undefined, 'an injection belongs to no language of its own');

  const rules = read(`../${contributed.path}`);
  assert.equal(rules.scopeName, contributed.scopeName);
  assert.match(rules.injectionSelector, /comment\.line/);
  assert.equal(rules.patterns.length, 1);

  const [rule] = rules.patterns;
  const match = new RegExp(rule.match);
  assert.ok(match.test('// @use stencil'), 'the marker itself');
  assert.ok(match.test('  // @use stencil — and a sentence after it'));
  assert.ok(match.test('# @use stencil'), 'the Python marker is the same words');
  assert.ok(!match.test('// @use stencils'), 'a longer word is a different word');
  assert.ok(!match.test('// use stencil'), 'the directive is part of the marker');
  // ONE scope a theme already knows, so the marker reads as a single declaration.
  assert.equal(rule.captures['1'].name, rule.captures['2'].name);
  assert.match(rule.captures['1'].name, /^keyword\.control\..*\.stc$/);
});

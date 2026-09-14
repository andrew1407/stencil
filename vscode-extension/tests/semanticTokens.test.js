// The classified tokens reach the editor as legend rows, in source order. WHICH type each
// token gets is tokenClassify.test.js; this is the legend, the rows and the registration.
import test from 'node:test';
import assert from 'node:assert/strict';

import { installVscodeStub, makeDocument, makeVscode } from './helpers/vscodeStub.js';

const withHost = (body) => {
  const { vscode, calls } = makeVscode();
  const host = installVscodeStub(vscode);
  try {
    return body({
      calls, host, classify: host.require('lib/tokenClassify.js'),
      tokens: host.require('semanticTokens.js'),
    });
  } finally {
    host.restore();
  }
};

test('the legend uses only standard VS Code token types', () => {
  withHost(({ tokens }) => {
    // The types VS Code REGISTERS, which is narrower than the LSP list: `modifier` is in the
    // spec and in no registry, so a token typed that way is left for the grammar to colour.
    const STANDARD = new Set([
      'class', 'comment', 'decorator', 'enum', 'enumMember', 'event', 'function', 'interface',
      'keyword', 'label', 'macro', 'member', 'method', 'namespace', 'number', 'operator',
      'parameter', 'property', 'regexp', 'string', 'struct', 'type', 'typeParameter', 'variable',
    ]);
    for (const type of tokens.TOKEN_TYPES) assert.ok(STANDARD.has(type), `${type} is standard`);
    assert.equal(new Set(tokens.TOKEN_TYPES).size, tokens.TOKEN_TYPES.length, 'no duplicates');
    assert.deepEqual(tokens.LEGEND.tokenTypes, tokens.TOKEN_TYPES);
  });
});

test('every type the classifier can emit is in the legend', () => {
  withHost(({ tokens, classify }) => {
    for (const table of [classify.GROUP_TYPE, classify.KIND_TYPE]) {
      for (const type of Object.values(table)) {
        assert.ok(tokens.TOKEN_TYPES.includes(type), `${type} is in the legend`);
      }
    }
    assert.deepEqual(Object.keys(tokens.TYPE_INDEX), [...tokens.TOKEN_TYPES]);
  });
});

test('tokenRows drops unclassified and zero-width tokens and goes 0-based', () => {
  withHost(({ tokens }) => {
    const rows = tokens.tokenRows([
      { line: 1, col: 1, len: 5, kind: 'directive', text: '@crop' },
      { line: 1, col: 9, len: 4, kind: 'ident', text: 'nope' },
      { line: 1, col: 14, len: 1, kind: 'punct', text: '\n' },
      { line: 2, col: 3, len: 0, kind: 'number', text: '' },
      { line: 2, col: 5, len: 2, kind: 'unit', text: 'px' },
    ]);
    assert.deepEqual(rows, [
      [0, 0, 5, tokens.TYPE_INDEX.keyword],
      [1, 4, 2, tokens.TYPE_INDEX.operator],
    ]);
  });
});

test('a real buffer colours the directive, the number, the unit and the comment', async () => {
  await withHost(async ({ tokens }) => {
    const document = makeDocument({ text: '@source a.png:\n    @crop 10%   # note\n' });
    const built = await tokens.provider.provideDocumentSemanticTokens(document);
    const type = (name) => tokens.TOKEN_TYPES.indexOf(name);
    const kinds = built.rows.map((r) => r[3]);
    assert.ok(kinds.includes(type('macro')), '@source opens a block');
    assert.ok(kinds.includes(type('keyword')), '@crop is an edit');
    assert.ok(kinds.includes(type('number')), '10 is a number');
    assert.ok(kinds.includes(type('operator')), '% is a unit');
    assert.ok(kinds.includes(type('comment')), '# note is a comment');
    for (let i = 1; i < built.rows.length; i += 1) {
      const [line, char] = built.rows[i];
      const [prevLine, prevChar] = built.rows[i - 1];
      assert.ok(line > prevLine || (line === prevLine && char >= prevChar), 'rows ascend');
    }
  });
});

test('a hex colour is a colour, not a comment', async () => {
  await withHost(async ({ tokens }) => {
    const document = makeDocument({ text: '@use line #ccc solid\n' });
    const built = await tokens.provider.provideDocumentSemanticTokens(document);
    const kinds = built.rows.map((r) => r[3]);
    assert.ok(kinds.includes(tokens.TOKEN_TYPES.indexOf('property')), '#ccc is a colour');
    assert.ok(!kinds.includes(tokens.TOKEN_TYPES.indexOf('comment')), 'nothing is a comment');
  });
});

test('register hands VS Code the provider and the legend for stencil-script', () => {
  withHost(({ calls, tokens }) => {
    const context = { subscriptions: [] };
    tokens.register(context);
    const [registered] = calls.semanticProviders;
    assert.deepEqual(registered.selector, { language: 'stencil-script' });
    assert.equal(registered.provider, tokens.provider);
    assert.equal(registered.legend, tokens.LEGEND);
    assert.equal(context.subscriptions.length, 1);
  });
});

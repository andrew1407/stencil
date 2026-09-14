// The lexer's TokenKinds reach the editor as legend rows, in source order.
import test from 'node:test';
import assert from 'node:assert/strict';

import { installVscodeStub, makeDocument, makeVscode } from './helpers/vscodeStub.js';

const withHost = (body) => {
  const { vscode, calls } = makeVscode();
  const host = installVscodeStub(vscode);
  try {
    return body({ calls, host, tokens: host.require('semanticTokens.js') });
  } finally {
    host.restore();
  }
};

test('the legend uses only standard VS Code token types', () => {
  withHost(({ tokens }) => {
    const STANDARD = new Set([
      'namespace', 'type', 'class', 'enum', 'interface', 'struct', 'typeParameter',
      'parameter', 'variable', 'property', 'enumMember', 'event', 'function', 'method',
      'macro', 'keyword', 'modifier', 'comment', 'string', 'number', 'regexp', 'operator',
      'decorator',
    ]);
    for (const type of tokens.TOKEN_TYPES) assert.ok(STANDARD.has(type), `${type} is standard`);
    assert.equal(new Set(tokens.TOKEN_TYPES).size, tokens.TOKEN_TYPES.length, 'no duplicates');
    assert.deepEqual(tokens.LEGEND.tokenTypes, tokens.TOKEN_TYPES);
  });
});

test('every mapped kind names a legend entry, and the noisy kinds stay out', () => {
  withHost(({ tokens }) => {
    for (const [kind, type] of Object.entries(tokens.KIND_TYPE)) {
      assert.ok(tokens.TOKEN_TYPES.includes(type), `${kind} → ${type} is in the legend`);
    }
    for (const kind of ['punct', 'ident', 'error']) {
      assert.equal(tokens.KIND_TYPE[kind], undefined, `${kind} carries no colour`);
    }
  });
});

test('tokenRows drops unmapped and zero-width tokens and goes 0-based', () => {
  withHost(({ tokens }) => {
    const rows = tokens.tokenRows([
      { line: 1, col: 1, len: 7, kind: 'directive' },
      { line: 1, col: 9, len: 5, kind: 'ident' },
      { line: 2, col: 3, len: 0, kind: 'number' },
      { line: 2, col: 5, len: 2, kind: 'unit' },
    ]);
    assert.deepEqual(rows, [
      [0, 0, 7, tokens.TOKEN_TYPES.indexOf('keyword')],
      [1, 4, 2, tokens.TOKEN_TYPES.indexOf('operator')],
    ]);
  });
});

test('a real buffer colours the directive, the number, the unit and the comment', async () => {
  await withHost(async ({ tokens }) => {
    const document = makeDocument({ text: '@source a.png:\n    @crop 10%   # note\n' });
    const built = await tokens.provider.provideDocumentSemanticTokens(document);
    const type = (name) => tokens.TOKEN_TYPES.indexOf(name);
    const kinds = built.rows.map((r) => r[3]);
    assert.ok(kinds.includes(type('keyword')), 'the directives are keywords');
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

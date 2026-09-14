// Colour from the parser rather than from the grammar: the TextMate rules paint a line at a
// time, the lexer knows the whole file, so `#ccc` stays a colour and `# note` a comment.
'use strict';

const vscode = require('vscode');

const { LANGUAGE_ID } = require('./lib/ids.js');
const { programFor } = require('./lib/programCache.js');

// Standard VS Code token types only: a theme that has never heard of .stc still colours it.
const TOKEN_TYPES = Object.freeze([
  'comment', 'keyword', 'number', 'operator', 'string', 'parameter', 'property', 'variable',
]);

// Lexer TokenKind → legend entry. `punct`, `ident` and `error` are deliberately absent:
// punctuation is the grammar's job and an unclassified word carries no colour of its own.
const KIND_TYPE = Object.freeze({
  comment: 'comment',
  directive: 'keyword',
  keyword: 'keyword',
  number: 'number',
  unit: 'operator',
  color: 'property',
  string: 'string',
  param: 'parameter',
});

const KIND_INDEX = Object.freeze(Object.fromEntries(Object.entries(KIND_TYPE)
  .map(([kind, type]) => [kind, TOKEN_TYPES.indexOf(type)])));
const LEGEND = new vscode.SemanticTokensLegend(TOKEN_TYPES, []);

// (line, char, length, typeIndex) rows, 0-based; an unmapped or zero-width token is skipped.
const tokenRows = (tokens) => {
  const rows = [];
  for (const token of tokens ?? []) {
    const index = KIND_INDEX[token.kind];
    if (index === undefined || !(token.len > 0)) continue;
    rows.push([token.line - 1, token.col - 1, token.len, index]);
  }
  return rows;
};

const provider = {
  async provideDocumentSemanticTokens(document) {
    const program = await programFor(document);
    const builder = new vscode.SemanticTokensBuilder(LEGEND);
    for (const [line, char, length, type] of tokenRows(program.tokens)) {
      builder.push(line, char, length, type);
    }
    return builder.build();
  },
};

const register = (context) => {
  const registration = vscode.languages.registerDocumentSemanticTokensProvider(
    { language: LANGUAGE_ID }, provider, LEGEND,
  );
  context.subscriptions.push(registration);
  return registration;
};

module.exports = { KIND_INDEX, KIND_TYPE, LEGEND, TOKEN_TYPES, provider, register, tokenRows };

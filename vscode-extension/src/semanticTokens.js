// Colour from the parser rather than from the grammar: the TextMate rules paint a line at a
// time, the lexer knows the whole file, so `#ccc` stays a colour and `# note` a comment.
// Which colour each token gets is lib/tokenClassify.js; this is the legend and the wiring.
'use strict';

const vscode = require('vscode');

const { LANGUAGE_ID } = require('./lib/ids.js');
const { programFor } = require('./lib/programCache.js');
const { classify } = require('./lib/tokenClassify.js');

// Standard VS Code token types only: a theme that has never heard of .stc still colours it.
const TOKEN_TYPES = Object.freeze([
  'comment', 'namespace', 'class', 'type', 'keyword', 'function', 'parameter',
  'enumMember', 'modifier', 'property', 'variable', 'string', 'number', 'operator',
]);

const TYPE_INDEX = Object.freeze(Object.fromEntries(TOKEN_TYPES.map((type, i) => [type, i])));
const LEGEND = new vscode.SemanticTokensLegend(TOKEN_TYPES, []);

// (line, char, length, typeIndex) rows, 0-based; an unclassified or zero-width token is skipped.
const tokenRows = (tokens) => {
  const types = classify(tokens);
  const rows = [];
  (tokens ?? []).forEach((token, i) => {
    const index = TYPE_INDEX[types[i]];
    if (index === undefined || !(token.len > 0)) return;
    rows.push([token.line - 1, token.col - 1, token.len, index]);
  });
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

module.exports = { LEGEND, TOKEN_TYPES, TYPE_INDEX, provider, register, tokenRows };

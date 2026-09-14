// What the word under the cursor means. The token under the position comes from the same
// parse the colours do, so a hover can never land on a word the editor is not colouring.
'use strict';

const vscode = require('vscode');

const { LANGUAGE_ID } = require('./lib/ids.js');
const { programFor } = require('./lib/programCache.js');
const { explain } = require('./lib/vocabulary.js');

// The token covering a 0-based (line, character), or undefined. Tokens carry 1-based spans.
const tokenAt = (tokens, position) => (tokens ?? []).find((token) => (
  token.line - 1 === position.line
  && position.character >= token.col - 1
  && position.character < token.col - 1 + token.len
));

/* Hovering a unit asks about the unit, not the number it hangs off; everything else asks
 * about its own text. A kind with nothing to say (a path, a point) yields no hover. */
const HOVERABLE = new Set(['directive', 'ident', 'unit', 'param']);

const PARAM_DOC = [
  '**`@1`** — a template parameter.',
  'Positional, substituted where it stands. The highest index a template mentions **is** its'
  + ' arity, so `@2` makes it take two arguments.',
  ['```stc', '@stencil callout:', '    @use line @1, @2', '', '@source a.png:',
    '    @use stencil callout red 3px:', '```'].join('\n'),
].join('\n\n');

const markdownAt = (tokens, position) => {
  const token = tokenAt(tokens, position);
  if (!token || !HOVERABLE.has(token.kind)) return '';
  if (token.kind === 'param') return PARAM_DOC;
  return explain(token.text);
};

const provider = {
  async provideHover(document, position) {
    const program = await programFor(document);
    const markdown = markdownAt(program.tokens, position);
    if (!markdown) return undefined;
    const contents = new vscode.MarkdownString(markdown);
    contents.supportHtml = false;
    return new vscode.Hover(contents);
  },
};

const register = (context) => {
  const registration = vscode.languages.registerHoverProvider({ language: LANGUAGE_ID }, provider);
  context.subscriptions.push(registration);
  return registration;
};

module.exports = { PARAM_DOC, markdownAt, provider, register, tokenAt };

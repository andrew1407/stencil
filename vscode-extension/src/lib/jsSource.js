// Which JavaScript buffers are Stencil's: a `.stcjs` always, a plain `.js` when its first
// line says `// @use stencil`. The document is passed in, so `vscode` is never imported.
'use strict';

const { JS_LANGUAGE_ID, USE_MARKER } = require('./ids.js');

const JS_LANGUAGE = 'javascript';

// The marker itself, explained. Nothing else says what that line does — it is a comment to
// JavaScript, so the editor has no opinion about it at all.
const MARKER_DOC = [
  "**The Stencil marker** — this file drives the browser app's `stencil` facade.",
  ['```js', USE_MARKER, '```'].join('\n'),
  'It may stand **anywhere** in the file, on a comment line of its own. With it there, a plain'
  + " `.js` is offered the facade's members and their explanations, exactly as a `.stcjs` is;"
  + ' take it away and the file is ordinary JavaScript again. A `.stcjs` needs no marker at all.',
  'Run **Stencil: Add facade typings to this workspace** to have the editor type `stencil` too.',
].join('\n\n');

// The same words with code in front of them: not the marker, and better said than ignored.
const MARKER_TRAILING_DOC = [
  '**The Stencil marker — not read here.** It is only the marker on a comment line of its own.',
  ['```js', USE_MARKER, '```'].join('\n'),
  'Give it its own line, anywhere in the file. Or rename the file to `.stcjs`, which needs no'
  + ' marker at all.',
].join('\n\n');

// The marker's span on its line — `@use stencil`, without the comment slashes. A hover is
// given this range, so the whole marker lights up rather than the one word under the pointer.
const markerSpan = (lineText) => {
  const text = String(lineText ?? '');
  const start = text.indexOf(USE_MARKER);
  if (start < 0 || text.slice(0, start).trim() !== '') return null;
  return { start: text.indexOf('@', start), end: start + USE_MARKER.length };
};

/* The marker's words under the caret, wherever on the line. An own-line marker also has a
 * `markerSpan`; one with code in front of it does not, and is explained instead. */
const markerWordsAt = (lineText, character) => {
  const text = String(lineText ?? '');
  const start = text.indexOf(USE_MARKER);
  if (start < 0) return null;
  const at = Number(character) || 0;
  const from = text.indexOf('@', start);
  const to = start + USE_MARKER.length;
  return at >= from && at <= to ? { start: from, end: to } : null;
};

// The first line carrying it, ANYWHERE — a whole-text scan, which is what "anywhere" costs.
const markerLine = (document) => {
  const lines = String(document?.getText?.() ?? '').split(/\r?\n/);
  for (let i = 0; i < lines.length; i += 1) {
    if (markerSpan(lines[i])) return i;
  }
  return -1;
};

/* A word inside a line comment is prose, not code: `stencil` written in a sentence should not
 * pop the facade's explanation. `https://` is not a comment, so `//` after a colon does not
 * count. */
const inLineComment = (lineText, character) =>
  /(^|[^:])\/\//.test(String(lineText ?? '').slice(0, Number(character) || 0));

// lineAt, not a split of the whole buffer: this is asked on every completion and hover.
const marksStencil = (document) => markerLine(document) >= 0;

// Any buffer the editor calls JavaScript, opted in or not: the marker is explained in all.
const isJsDocument = (document) => !!document
  && (document.languageId === JS_LANGUAGE_ID || document.languageId === JS_LANGUAGE);

const isJsSource = (document) => !!document && (document.languageId === JS_LANGUAGE_ID
  || (document.languageId === JS_LANGUAGE && marksStencil(document)));

module.exports = { JS_LANGUAGE, MARKER_DOC, MARKER_TRAILING_DOC, inLineComment, isJsDocument,
  isJsSource, markerLine, markerSpan, markerWordsAt, marksStencil };

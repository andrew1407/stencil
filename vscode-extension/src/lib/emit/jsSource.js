// Which JavaScript buffers are Stencil's: a `.stcjs` always, a plain `.js` when a comment
// line of its own says `// @use stencil`. The document is passed in, so `vscode` is never imported.
import { JS_LANGUAGE_ID, USE_MARKER } from '../ids.js';
import { versionCache } from '../spawn/versionCache.js';

const JS_LANGUAGE = 'javascript';

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

// `@use stencil` on a line of its own, without the slashes: the range a hover lights up whole.
const markerSpan = (lineText) => {
  const text = String(lineText ?? '');
  const start = text.indexOf(USE_MARKER);
  if (start < 0 || text.slice(0, start).trim() !== '') return null;
  return { start: text.indexOf('@', start), end: start + USE_MARKER.length };
};

// The marker's words under the caret; with code in front they have no `markerSpan` and are explained.
const markerWordsAt = (lineText, character) => {
  const text = String(lineText ?? '');
  const start = text.indexOf(USE_MARKER);
  if (start < 0) return null;
  const at = Number(character) || 0;
  const from = text.indexOf('@', start);
  const to = start + USE_MARKER.length;
  return at >= from && at <= to ? { start: from, end: to } : null;
};

// The first line carrying it, anywhere: a whole-text scan, so one scan stands for one edit.
const marked = versionCache();

const markerLine = (document) => {
  const lines = String(document?.getText?.() ?? '').split(/\r?\n/);
  for (let i = 0; i < lines.length; i += 1) {
    if (markerSpan(lines[i])) return i;
  }
  return -1;
};

// The first `//` outside a string, taking `https://` at face value — the reading that stands
// where the quotes could not be followed.
const PLAIN_SLASHES = /(?:^|[^:])(\/\/)/;

/* Where a line comment opens, or -1, tracking quotes; a line ENDING inside a quote was misread
 * (a regex holding an apostrophe, a string being typed), so the plain reading stands in. */
const commentStart = (text) => {
  let quote = '';
  for (let i = 0; i < text.length; i += 1) {
    const char = text[i];
    if (quote) {
      if (char === '\\') i += 1;
      else if (char === quote) quote = '';
    } else if (char === '"' || char === "'" || char === '`') quote = char;
    else if (char === '/' && text[i + 1] === '/') return i;
  }
  const plain = quote ? PLAIN_SLASHES.exec(text) : null;
  return plain ? plain.index + plain[0].length - 2 : -1;
};

// A word inside a line comment is prose: `stencil` in a sentence pops no explanation.
const inLineComment = (lineText, character) => {
  const start = commentStart(String(lineText ?? ''));
  return start >= 0 && (Number(character) || 0) >= start + 2;
};

const marksStencil = (document) => marked.get(document, (buffer) => markerLine(buffer) >= 0);

// Any buffer the editor calls JavaScript, opted in or not: the marker is explained in all.
const isJsDocument = (document) => !!document
  && (document.languageId === JS_LANGUAGE_ID || document.languageId === JS_LANGUAGE);

const isJsSource = (document) => !!document && (document.languageId === JS_LANGUAGE_ID
  || (document.languageId === JS_LANGUAGE && marksStencil(document)));

export { JS_LANGUAGE, MARKER_DOC, MARKER_TRAILING_DOC, commentStart, inLineComment,
  isJsDocument, isJsSource, markerLine, markerSpan, markerWordsAt, marksStencil };

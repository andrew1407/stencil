// Which Python buffers are Stencil's: a `.pystc` always, a plain `.py` when a comment line
// of its own says `# @use stencil`. The document is passed in, so `vscode` is never imported.
// Twin of lib/jsSource.js, whose marker is the `//` one.
import { PY_LANGUAGE_ID, PY_USE_MARKER } from '../ids.js';
import { versionCache } from '../spawn/versionCache.js';

const PY_LANGUAGE = 'python';

// The marker's span on its line — `@use stencil`, without the `#`. Only a line that carries
// nothing before it: with code in front, the words are prose in a trailing comment.
const markerSpan = (lineText) => {
  const text = String(lineText ?? '');
  const start = text.indexOf(PY_USE_MARKER);
  if (start < 0 || text.slice(0, start).trim() !== '') return null;
  return { start: text.indexOf('@', start), end: start + PY_USE_MARKER.length };
};

const marked = versionCache();

const markerLine = (document) => {
  const lines = String(document?.getText?.() ?? '').split(/\r?\n/);
  for (let i = 0; i < lines.length; i += 1) {
    if (markerSpan(lines[i])) return i;
  }
  return -1;
};

const marksStencil = (document) => marked.get(document, (buffer) => markerLine(buffer) >= 0);

// Any buffer the editor calls Python, opted in or not.
const isPyDocument = (document) => !!document
  && (document.languageId === PY_LANGUAGE_ID || document.languageId === PY_LANGUAGE);

const isPySource = (document) => !!document && (document.languageId === PY_LANGUAGE_ID
  || (document.languageId === PY_LANGUAGE && marksStencil(document)));

export { PY_LANGUAGE, isPyDocument, isPySource, markerLine, markerSpan, marksStencil };

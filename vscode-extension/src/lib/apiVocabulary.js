// The one reading of src/config/stencilApiVocabulary.json: what a `window.stencil` member is
// and the Markdown explaining it. No `vscode` — jsHints.js makes the editor objects.
'use strict';

const { makeExplain } = require('./vocabularyEntry.js');

const MEMBERS = Object.freeze(require('../config/stencilApiVocabulary.json').members);
const MEMBER_NAMES = Object.freeze(Object.keys(MEMBERS));

// Anchored on a non-identifier, so `myStencil.` is somebody else's object.
const MEMBER_PREFIX = /(?:^|[^A-Za-z0-9_$.])(?:window\s*\.\s*)?stencil\s*\.\s*([A-Za-z0-9_$]*)$/;
const IDENT = /[A-Za-z0-9_$]/;
const FACADE = 'stencil';
// `window.` is the only thing the global may hang off; anything else owns its own `stencil`.
const WINDOW_BEFORE = /(?:^|[^A-Za-z0-9_$.])window\s*\.$/;

/* The global itself. The editor's JavaScript service sees a name nothing declares and says
 * `any`; this says what it is, and where it exists. */
const FACADE_DOC = [
  "**`stencil`** — the browser app's console control API (`window.stencil`).",
  ['```js', "stencil.rotateRight().apply({ filter: 'sepia' })", '```'].join('\n'),
  'A frozen, guarded facade over the live editor: every mutation goes through the same core'
  + ' methods the toolbar uses, and most calls hand the facade back, so they chain. The page'
  + ' installs it, so it exists only where the app is running \u2014 run this file with'
  + ' **Stencil: Run in Stencil Web Console**.',
].join('\n\n');

// The identifier the character sits inside, as [word, start].
const wordAt = (lineText, character) => {
  const text = String(lineText ?? '');
  let start = Math.min(Math.max(Number(character) || 0, 0), text.length);
  let end = start;
  while (start > 0 && IDENT.test(text[start - 1])) start -= 1;
  while (end < text.length && IDENT.test(text[end])) end += 1;
  return [text.slice(start, end), start, text];
};

const entryFor = (name) => MEMBERS[String(name)];

// The part-word after a `stencil.`, else null — how a `.` elsewhere offers nothing.
const prefixAt = (linePrefix) => {
  const match = MEMBER_PREFIX.exec(String(linePrefix ?? ''));
  return match ? match[1] : null;
};

// The whole member name the character sits inside, if it is one reached through the facade.
const memberAt = (lineText, character) => {
  const [word, start, text] = wordAt(lineText, character);
  if (!word || !Object.hasOwn(MEMBERS, word)) return '';
  return prefixAt(text.slice(0, start)) === '' ? word : '';
};

// The caret on the global itself — bare, or as the `stencil` in `window.stencil`.
const facadeAt = (lineText, character) => {
  const [word, start, text] = wordAt(lineText, character);
  if (word !== FACADE) return false;
  const before = text.slice(0, start).replace(/\s+$/, '');
  return !before.endsWith('.') || WINDOW_BEFORE.test(before);
};

const explain = makeExplain({
  lookup: entryFor,
  label: (name) => `stencil.${name}`,
  fence: 'js',
  decorate: (markdown, entry) => (entry.readOnly ? `${markdown}\n\nRead-only.` : markdown),
});

module.exports = { FACADE_DOC, MEMBER_NAMES, entryFor, explain, facadeAt, memberAt,
  prefixAt };

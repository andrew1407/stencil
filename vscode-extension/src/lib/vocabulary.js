// The one reading of src/config/stcVocabulary.json: what a word is, and the Markdown that
// explains it. No `vscode` here — hover.js and completion.js turn these into editor objects.
'use strict';

const VOCABULARY = require('../config/stcVocabulary.json');

const DIRECTIVES = VOCABULARY.directives;
const WORDS = VOCABULARY.words;
const UNITS = VOCABULARY.units;

const DIRECTIVE_NAMES = Object.freeze(Object.keys(DIRECTIVES));
const UNIT_NAMES = Object.freeze(Object.keys(UNITS));

const namesInGroup = (group) =>
  Object.freeze(Object.keys(WORDS).filter((word) => WORDS[word].group === group));

const MODES = namesInGroup('mode');
const STYLES = namesInGroup('style');
const CROP_KEYS = namesInGroup('key');

const groupOf = (directive) => DIRECTIVES[String(directive).toLowerCase()]?.group;

const entryFor = (word) => {
  const key = String(word).toLowerCase();
  return DIRECTIVES[key] ?? WORDS[key] ?? UNITS[key];
};

/* `**@crop** — Cut the picture down…` over a fenced signature, the prose, then a fenced
 * example. Every section is optional, so a one-line word renders as one line. */
const markdownFor = (label, entry) => {
  if (!entry) return '';
  const parts = [`**${label}** — ${entry.summary}`];
  if (entry.signature) parts.push(['```stc', entry.signature, '```'].join('\n'));
  if (entry.detail) parts.push(entry.detail);
  if (entry.example) parts.push(['```stc', entry.example, '```'].join('\n'));
  return parts.join('\n\n');
};

// Directives and keywords are case-insensitive, so the heading shows the canonical spelling.
const explain = (word) => {
  const bare = String(word).replace(/^@/, '').toLowerCase();
  const entry = entryFor(bare);
  if (!entry) return '';
  return markdownFor(DIRECTIVES[bare] === entry ? `@${bare}` : bare, entry);
};

module.exports = {
  CROP_KEYS, DIRECTIVES, DIRECTIVE_NAMES, MODES, STYLES, UNITS, UNIT_NAMES, WORDS,
  entryFor, explain, groupOf, markdownFor,
};

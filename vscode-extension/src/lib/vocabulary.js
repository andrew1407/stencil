// The one reading of src/config/stcVocabulary.json: what a word is, and the Markdown that
// explains it. No `vscode` here — hover.js and completion.js turn these into editor objects.
'use strict';

const { makeExplain } = require('./vocabularyEntry.js');

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

// Directives and keywords are case-insensitive, so the heading shows the canonical spelling.
const explain = makeExplain({
  normalize: (word) => String(word).replace(/^@/, '').toLowerCase(),
  lookup: entryFor,
  label: (bare, entry) => (DIRECTIVES[bare] === entry ? `@${bare}` : bare),
});

module.exports = {
  CROP_KEYS, DIRECTIVES, DIRECTIVE_NAMES, MODES, STYLES, UNITS, UNIT_NAMES, WORDS,
  entryFor, explain, groupOf,
};

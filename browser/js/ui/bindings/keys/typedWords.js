// A word typed into the bare window opens its logo show. One listener, one string compare per
// printable key: no timers, and every key is the caret's while any field holds it.
import { isTypingTarget } from '../../../utils.js';
import { TYPED_WORDS, SHOW_NAMES } from '../../logo/stageRules.js';
import { activateShow } from '../../logo/stageTrigger.js';

const LONGEST = TYPED_WORDS.reduce((n, w) => Math.max(n, w.length), 0);

// The show a buffer ends with, or null. Exported for the suite.
export const matchTypedWord = (buffer) => {
  const i = TYPED_WORDS.findIndex((w) => buffer.endsWith(w));
  return i < 0 ? null : SHOW_NAMES[i];
};

// The letter a key spells: the layout's own when it is Latin, else the one the same physical
// key carries on the US layout, so a word typed under a Cyrillic layout still lands.
export const typedLetter = (e) => {
  const key = typeof e.key === 'string' && e.key.length === 1 ? e.key.toLowerCase() : '';
  if (/^[a-z]$/.test(key)) return key;
  const physical = /^Key([A-Z])$/.exec(e.code || '');
  return physical ? physical[1].toLowerCase() : key;
};

export function wireTypedWords(app, doc = document) {
  let buffer = '';
  doc.addEventListener('keydown', (e) => {
    if (e.ctrlKey || e.metaKey || e.altKey ||
        isTypingTarget(e.target) || isTypingTarget(doc.activeElement)) { buffer = ''; return; }
    const letter = typedLetter(e);
    if (!letter) return;
    buffer = (buffer + letter).slice(-LONGEST);
    const name = matchTypedWord(buffer);
    if (!name) return;
    buffer = '';
    activateShow(name, app);
  });
}

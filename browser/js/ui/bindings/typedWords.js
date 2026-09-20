// A word typed into the bare window opens its logo show. One listener, one string compare per
// printable key: no timers, and a focused field keeps every key it is given.
import { isTypingTarget } from '../../utils.js';
import { TYPED_WORDS, SHOW_NAMES } from '../logoStageRules.js';
import { activateShow } from '../logoStageTrigger.js';

const LONGEST = TYPED_WORDS.reduce((n, w) => Math.max(n, w.length), 0);

// The show a buffer ends with, or null. Exported for the suite.
export const matchTypedWord = (buffer) => {
  const i = TYPED_WORDS.findIndex((w) => buffer.endsWith(w));
  return i < 0 ? null : SHOW_NAMES[i];
};

export function wireTypedWords(app, doc = document) {
  let buffer = '';
  doc.addEventListener('keydown', (e) => {
    if (e.ctrlKey || e.metaKey || e.altKey || isTypingTarget(e.target)) { buffer = ''; return; }
    if (typeof e.key !== 'string' || e.key.length !== 1) return;
    buffer = (buffer + e.key.toLowerCase()).slice(-LONGEST);
    const name = matchTypedWord(buffer);
    if (!name) return;
    buffer = '';
    activateShow(name, app);
  });
}

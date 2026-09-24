// A show's name typed outside a field opens it — over a running one too, since it captures ahead
// of the stage's key swallow. A letter more than TYPE_GAP_MS after the last starts a new word.
// Browser twin: js/ui/bindings/keys/typedWords.js.
import { TYPED_WORDS, SHOW_NAMES, TYPE_GAP_MS } from '../../lib/logo/stageRules.js';
import { activateShow } from './trigger.js';

const LONGEST = TYPED_WORDS.reduce((n, w) => Math.max(n, w.length), 0);

// browser utils/dom.js isTypingTarget: every key is the caret's while a field holds it.
export const isTypingTarget = t => {
  if (!t) return false;
  const tag = (t.tagName || '').toLowerCase();
  if (tag === 'textarea') return true;
  if (tag === 'select') return true;
  if (tag === 'input') {
    const ty = (t.type || '').toLowerCase();
    return !(ty === 'checkbox' || ty === 'radio' || ty === 'file' || ty === 'color' || ty === 'button');
  }
  return t.isContentEditable === true;
};

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

export function wireTypedWords(doc = document) {
  let buffer = '';
  let last = 0;
  doc.addEventListener('keydown', (e) => {
    if (e.ctrlKey || e.metaKey || e.altKey ||
        isTypingTarget(e.target) || isTypingTarget(doc.activeElement)) { buffer = ''; return; }
    const letter = typedLetter(e);
    if (!letter) return;
    const now = typeof e.timeStamp === 'number' && e.timeStamp > 0 ? e.timeStamp : Date.now();
    if (now - last > TYPE_GAP_MS) buffer = '';
    last = now;
    buffer = (buffer + letter).slice(-LONGEST);
    const name = matchTypedWord(buffer);
    if (!name) return;
    buffer = '';
    activateShow(name, null, { replace: true });
  }, true);
}

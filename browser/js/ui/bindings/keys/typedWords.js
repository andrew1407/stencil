// A word typed outside a field opens its logo show — over a running one too, since it captures
// ahead of the stage's key swallow. Every key is the caret's while any field holds it. A letter
// more than TYPE_GAP_MS after the last starts a new word: "neo" never joins a later "n".
import { isTypingTarget } from '../../../utils.js';
import { TYPED_WORDS, SHOW_NAMES, TYPE_GAP_MS } from '../../logo/stageRules.js';
import { activateShow } from '../../logo/stageTrigger.js';
import { CLI_MODE } from '../../../console/shell/table.js';
import { enterCliMode } from '../../cli/mode.js';

const LONGEST = TYPED_WORDS.reduce((n, w) => Math.max(n, w.length), 0);

// The show a buffer ends with, or null. Exported for the suite.
export const matchTypedWord = (buffer) => {
  const i = TYPED_WORDS.findIndex((w) => buffer.endsWith(w));
  return i < 0 ? null : SHOW_NAMES[i];
};

// The CLI mode's word is typed like a show's; it opens the terminal instead (ui/cli/mode.js).
export const CLI_WORD = CLI_MODE.typedWord.toLowerCase();
const KEEP = Math.max(LONGEST, CLI_WORD.length);

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
  let last = 0;
  doc.addEventListener('keydown', (e) => {
    if (e.ctrlKey || e.metaKey || e.altKey ||
        isTypingTarget(e.target) || isTypingTarget(doc.activeElement)) { buffer = ''; return; }
    const letter = typedLetter(e);
    if (!letter) return;
    const now = typeof e.timeStamp === 'number' && e.timeStamp > 0 ? e.timeStamp : Date.now();
    if (now - last > TYPE_GAP_MS) buffer = '';
    last = now;
    buffer = (buffer + letter).slice(-KEEP);
    if (buffer.endsWith(CLI_WORD)) {
      buffer = '';
      enterCliMode(app);
      return;
    }
    const name = matchTypedWord(buffer);
    if (!name) return;
    buffer = '';
    activateShow(name, app, null, { replace: true });
  }, true);
}

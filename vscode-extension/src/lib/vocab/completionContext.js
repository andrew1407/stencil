// What may legally follow the caret, decided from the text before it on the line. The tables
// below are the whole specification; completion.js turns a group name into items.

const SEPARATORS = /[\s,()=:;]+/;
const NUMBER_STEM = /^(-?\d+(?:\.\d+)?)([a-z%]*)$/i;

// `use` splits again on its sub-keyword, so its groups live in a second table.
const AFTER = Object.freeze({
  filter: ['mode', 'color'],
  crop: ['key', 'unit'],
  line: ['unit'],
  rect: ['unit'],
  frame: [],
  source: [],
  save: [],
  stencil: [],
  undo: [],
  redo: [],
});

const AFTER_USE = Object.freeze({
  null: ['useSub', 'unit'],
  line: ['style', 'color', 'unit'],
  stencil: ['template'],
});

// VS Code filters on the partial word, so the context comes from what precedes it.
const splitPrefix = (linePrefix) => {
  const parts = String(linePrefix).split(SEPARATORS);
  return { words: parts.slice(0, -1).filter(Boolean), partial: parts[parts.length - 1] ?? '' };
};

// `@1` is a parameter, not a directive, so a template body keeps its enclosing context.
const DIRECTIVE_WORD = /^@[a-z]/i;

const groupsFor = (words) => {
  let at = -1;
  for (let i = words.length - 1; i >= 0; i -= 1) {
    if (DIRECTIVE_WORD.test(words[i])) { at = i; break; }
  }
  if (at < 0) return ['directive'];
  const directive = words[at].slice(1).toLowerCase();
  const rest = words.slice(at + 1);
  if (directive === 'use') return AFTER_USE[rest[0]?.toLowerCase() ?? 'null'] ?? [];
  if (directive === 'layout') return rest.length > 0 ? ['layoutMode'] : [];
  return AFTER[directive] ?? [];
};

/**
 * The suggestion groups for a caret at the end of `linePrefix`.
 * `numberStem` is set when the partial word is a length being typed (`10`, `10p`), so the
 * unit items can be offered as whole replacements of it.
 */
const contextFor = (linePrefix) => {
  const { words, partial } = splitPrefix(linePrefix);
  if (partial.startsWith('@')) return { groups: ['directive'], numberStem: null };
  const stem = NUMBER_STEM.exec(partial);
  if (stem) return { groups: ['unit'], numberStem: stem[1] };
  return { groups: groupsFor(words), numberStem: null };
};

export { AFTER, AFTER_USE, contextFor, groupsFor, splitPrefix };

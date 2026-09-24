// The lexer hands back one `directive` kind and one `ident` kind, because that is all the
// core needs; the colour a reader wants depends on which directive a word sits under.
import COLOR_NAMES from '../../config/colorNames.json' with { type: 'json' };
import { CROP_KEYS, MODES, STYLES, groupOf } from './vocabulary.js';

// A directive's family → its legend type. The types are picked for what the default themes
// paint them: namespace/type/class are one colour, so a block opener cannot be `namespace`.
const GROUP_TYPE = Object.freeze({
  source: 'macro', template: 'class', edit: 'keyword', output: 'function',
});

// Kinds the lexer already settles; an ident is the only one that needs the statement.
const KIND_TYPE = Object.freeze({
  comment: 'comment', param: 'parameter', color: 'property',
  string: 'string', number: 'number', unit: 'operator',
});

const UNIT_WORDS = new Set(['px', 'cm', 'mm', 'in', '%']);
const isColor = (word) => word === 'transparent' || Object.hasOwn(COLOR_NAMES, word);

// The sub-keyword right after `@use` first, then what it or the directive makes the rest.
const identType = (word, state) => {
  const { directive, sub, argIndex } = state;
  if (directive === 'use' && argIndex === 0) {
    if (word === 'line' || word === 'stencil') { state.sub = word; return 'keyword'; }
    return UNIT_WORDS.has(word) ? 'operator' : undefined;
  }
  if (sub === 'stencil' || directive === 'stencil') return 'type';
  if (directive === 'filter' && MODES.includes(word)) return 'enumMember';
  if (directive === 'crop' && CROP_KEYS.includes(word)) return 'variable';
  if (directive === 'layout' && STYLES.includes(word)) return 'label';
  if (sub === 'line' && STYLES.includes(word)) return 'label';
  // The spec, the target, the path: the one argument that names something outside the file.
  if (argIndex === 0 && (directive === 'source' || directive === 'save' || directive === 'layout')) {
    return 'string';
  }
  if (isColor(word)) return 'property';
  return UNIT_WORDS.has(word) ? 'operator' : undefined;
};

const classify = (tokens) => {
  const state = { directive: null, sub: null, argIndex: 0 };
  return (tokens ?? []).map((token) => {
    const text = String(token.text ?? '');
    if (token.kind === 'punct') {
      if (text === '\n' || text === ';') Object.assign(state, { directive: null, sub: null, argIndex: 0 });
      return undefined;
    }
    if (token.kind === 'directive') {
      Object.assign(state, { directive: text.replace(/^@/, '').toLowerCase(), sub: null, argIndex: 0 });
      return GROUP_TYPE[groupOf(state.directive)] ?? 'keyword';
    }
    const type = token.kind === 'ident'
      ? identType(text.toLowerCase(), state)
      : KIND_TYPE[token.kind];
    state.argIndex += 1;
    return type;
  });
};

export { GROUP_TYPE, KIND_TYPE, classify };

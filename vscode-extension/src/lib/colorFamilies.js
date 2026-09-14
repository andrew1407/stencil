// The names a reader would use for the things .stc colours, and the legend type each maps to.
// `stencil.colors` is keyed by these, not by VS Code's token types: a user setting a colour
// for `@source` should not have to know it is contributed as `macro`. No `vscode` here.
'use strict';

// family → the semantic token type tokenClassify.js emits for it.
const FAMILY_TYPE = Object.freeze({
  source: 'macro',
  template: 'class',
  templateName: 'type',
  edit: 'keyword',
  output: 'function',
  parameter: 'parameter',
  filterMode: 'enumMember',
  lineStyle: 'label',
  cropEdge: 'variable',
  colorValue: 'property',
  path: 'string',
  number: 'number',
  unit: 'operator',
  comment: 'comment',
});

const FAMILIES = Object.freeze(Object.keys(FAMILY_TYPE));

const HEX = /^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/;

/* The families the extension colours itself, because the token type each lands on means
 * something else to a theme: a path is not a string, a filter mode is not an enum member, and
 * @save is not a function. Two palettes, from the editor's own Dark+ and Light+. */
const DEFAULTS = Object.freeze({
  dark: Object.freeze({
    source: '#569cd6', output: '#569cd6', unit: '#569cd6', template: '#569cd6',
    filterMode: '#dcdcaa', path: '#d5a07a', templateName: '#d5a07a', cropEdge: '#4ec9b0',
  }),
  light: Object.freeze({
    source: '#0451a5', output: '#0451a5', unit: '#0451a5', template: '#0451a5',
    filterMode: '#795e26', path: '#9c5a33', templateName: '#9c5a33', cropEdge: '#267f99',
  }),
});

const defaultsFor = (light) => (light ? DEFAULTS.light : DEFAULTS.dark);

/**
 * The `stencil.colors` setting laid over those defaults and reduced to legend type → colour.
 * A family this language does not have is ignored, and so is a value that is not a hex colour;
 * an empty string means "leave it to the theme", which is how a row is handed back.
 */
const overridesFor = (setting, { light = false } = {}) => {
  const merged = { ...defaultsFor(light) };
  for (const [family, value] of Object.entries(setting ?? {})) {
    if (!Object.hasOwn(FAMILY_TYPE, family) || typeof value !== 'string') continue;
    const text = value.trim();
    if (text === '') delete merged[family];
    else if (HEX.test(text)) merged[family] = text;
  }
  return Object.fromEntries(Object.entries(merged).map(([f, color]) => [FAMILY_TYPE[f], color]));
};

module.exports = { DEFAULTS, FAMILIES, FAMILY_TYPE, HEX, defaultsFor, overridesFor };

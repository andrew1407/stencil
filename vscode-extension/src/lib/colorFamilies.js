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

/**
 * The `stencil.colors` setting reduced to legend type → colour, dropping anything unusable:
 * a family this language does not have, and a value that is not a hex colour. An empty string
 * means "leave it to the theme", which is how a row is turned off again.
 */
const overridesFor = (setting) => {
  const out = {};
  for (const [family, value] of Object.entries(setting ?? {})) {
    const type = FAMILY_TYPE[family];
    if (type && typeof value === 'string' && HEX.test(value.trim())) out[type] = value.trim();
  }
  return out;
};

module.exports = { FAMILIES, FAMILY_TYPE, HEX, overridesFor };

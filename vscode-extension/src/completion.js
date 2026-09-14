// The suggestion list. lib/completionContext.js decides WHICH groups belong at the caret;
// this turns a group into items, each carrying the same Markdown the hover shows.
'use strict';

const vscode = require('vscode');

const COLOR_NAMES = require('./config/colorNames.json');
const { LANGUAGE_ID } = require('./lib/ids.js');
const { contextFor } = require('./lib/completionContext.js');
const { programFor } = require('./lib/programCache.js');
const {
  CROP_KEYS, DIRECTIVE_NAMES, MODES, STYLES, UNITS, UNIT_NAMES, explain, markdownFor,
} = require('./lib/vocabulary.js');

const KIND = () => vscode.CompletionItemKind;
const COLOR_WORDS = Object.freeze([...Object.keys(COLOR_NAMES), 'transparent']);
const LAYOUT_MODES = Object.freeze(['combine', 'replace']);
const USE_SUBS = Object.freeze(['line', 'stencil']);

/* Template names the file itself defines: a `@stencil` token, then every ident up to its
 * `:`. The lowerer does not hand these back, and the token stream survives a broken parse. */
const templateNames = (tokens) => {
  const names = [];
  let collecting = null;
  for (const token of tokens ?? []) {
    if (token.kind === 'directive' && /^@stencil$/i.test(token.text)) { collecting = []; continue; }
    if (collecting === null) continue;
    if (token.kind === 'ident') collecting.push(token.text);
    else { if (collecting.length) names.push(collecting.join(' ')); collecting = null; }
  }
  return names;
};

const item = (label, kind, markdown, { insert = label, sortAs } = {}) => {
  const entry = new vscode.CompletionItem(label, kind);
  entry.insertText = insert;
  if (markdown) entry.documentation = new vscode.MarkdownString(markdown);
  if (sortAs) entry.sortText = sortAs;
  return entry;
};

const wordItems = (words, kind) =>
  words.map((word) => item(word, kind, explain(word)));

// `@crop` inserts as `@crop`; the leading `@` the user already typed is replaced by VS Code,
// which treats it as part of the word.
const directiveItems = () => DIRECTIVE_NAMES.map((name) =>
  item(`@${name}`, KIND().Keyword, explain(name)));

const unitItems = (stem) => UNIT_NAMES.map((unit) => item(
  stem === null ? unit : `${stem}${unit}`,
  KIND().Unit,
  markdownFor(unit, UNITS[unit]),
));

const colorItems = () => COLOR_WORDS.map((name) => {
  const entry = item(name, KIND().Color, explain(name));
  entry.detail = COLOR_NAMES[name] ?? 'no colour at all';
  // Colour names are a long tail; they sort under the words that carry meaning here.
  entry.sortText = `z${name}`;
  return entry;
});

const GROUP_ITEMS = Object.freeze({
  directive: directiveItems,
  mode: () => wordItems(MODES, KIND().EnumMember),
  style: () => wordItems(STYLES, KIND().Property),
  key: () => wordItems(CROP_KEYS, KIND().Field),
  layoutMode: () => wordItems(LAYOUT_MODES, KIND().EnumMember),
  useSub: () => USE_SUBS.map((word) => item(word, KIND().Keyword,
    explain(word) || `**${word}** — see \`@use\`.`)),
  color: colorItems,
});

/// The items for one caret. `tokens` supplies the template names; everything else is static.
const itemsFor = (linePrefix, tokens) => {
  const { groups, numberStem } = contextFor(linePrefix);
  return groups.flatMap((group) => {
    if (group === 'unit') return unitItems(numberStem);
    if (group === 'template') {
      return templateNames(tokens).map((name) => item(name, KIND().Function,
        `**${name}** — a template defined in this file.`));
    }
    return GROUP_ITEMS[group]?.() ?? [];
  });
};

const provider = {
  async provideCompletionItems(document, position) {
    const line = document.getText().split(/\r?\n/)[position.line] ?? '';
    const program = await programFor(document);
    return itemsFor(line.slice(0, position.character), program.tokens);
  },
};

const register = (context) => {
  const registration = vscode.languages.registerCompletionItemProvider(
    { language: LANGUAGE_ID }, provider, '@', '%',
  );
  context.subscriptions.push(registration);
  return registration;
};

module.exports = { COLOR_WORDS, LAYOUT_MODES, USE_SUBS, itemsFor, provider, register, templateNames };

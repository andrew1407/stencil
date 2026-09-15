// `stencil.` in JavaScript: the same kind of explanation the .stc hover gives, offered in a
// .stcjs and in any .js that opted in. Additive to the editor's own JavaScript service, which
// cannot know about a facade a page installs at runtime.
'use strict';

const vscode = require('vscode');

const { CONFIG_SECTION, JS_LANGUAGE_ID, SETTINGS } = require('./lib/ids.js');
const { JS_LANGUAGE, MARKER_DOC, MARKER_TRAILING_DOC, inLineComment, isJsDocument, isJsSource,
  markerSpan, markerWordsAt } = require('./lib/jsSource.js');
const { installedIn } = require('./lib/typingsFile.js');
const { FACADE_DOC, MEMBER_NAMES, entryFor, explain, facadeAt, memberAt,
  prefixAt } = require('./lib/apiVocabulary.js');

// Both flavours are offered to VS Code; `isJsSource` then decides per request, so turning a
// plain .js into a Stencil one needs no reload.
const SELECTOR = Object.freeze([{ language: JS_LANGUAGE_ID }, { language: JS_LANGUAGE }]);

const enabled = (setting) =>
  vscode.workspace.getConfiguration(CONFIG_SECTION).get(setting, true);

/* Where the workspace holds the facade's types, the editor's own JavaScript service already
 * answers — with the type, the prose and the example, from the same source this table is
 * written from — so answering too would only stack a second box under it. A .stcjs is never
 * TypeScript's, so there is nothing to stand aside for. */
const typescriptAnswers = (document) => document.languageId === JS_LANGUAGE
  && installedIn(vscode.workspace.getWorkspaceFolder?.(document.uri)?.uri?.fsPath);

const answerFor = (document, setting) =>
  isJsSource(document) && enabled(setting) && !typescriptAnswers(document);

const callable = (name) => entryFor(name)?.signature?.includes('(') ?? false;

const item = (name) => {
  const kind = callable(name) ? vscode.CompletionItemKind.Function : vscode.CompletionItemKind.Property;
  const entry = new vscode.CompletionItem(name, kind);
  entry.insertText = name;
  entry.detail = entryFor(name)?.summary;
  entry.documentation = new vscode.MarkdownString(explain(name));
  return entry;
};

const itemsFor = (linePrefix) => (prefixAt(linePrefix) === null ? [] : MEMBER_NAMES.map(item));

const completionProvider = {
  provideCompletionItems(document, position) {
    if (!answerFor(document, SETTINGS.completion)) return [];
    const line = document.lineAt(position.line).text;
    if (inLineComment(line, position.character)) return [];   // prose, as the hover reads it
    return itemsFor(line.slice(0, position.character));
  },
};

// `range` is what the editor underlines while the tooltip is up; without one it picks the
// word under the pointer, which splits a two-word marker in half.
const hover = (markdown, range) => {
  const contents = new vscode.MarkdownString(markdown);
  contents.supportHtml = false;
  return new vscode.Hover(contents, range);
};

const hoverProvider = {
  provideHover(document, position) {
    if (!isJsDocument(document) || !enabled(SETTINGS.hover)) return undefined;
    const line = document.lineAt(position.line).text;
    /* Answered for in ANY JavaScript buffer, opted in or not, and even where the types are
     * installed: it is a comment, which no language service explains, and one written where it
     * cannot be read is precisely the case that needs saying out loud. */
    const marker = markerWordsAt(line, position.character);
    if (marker) {
      return hover(markerSpan(line) ? MARKER_DOC : MARKER_TRAILING_DOC,
        new vscode.Range(position.line, marker.start, position.line, marker.end));
    }
    if (!isJsSource(document)) return undefined;
    if (typescriptAnswers(document) || inLineComment(line, position.character)) return undefined;
    // The member, else the global itself.
    const markdown = explain(memberAt(line, position.character))
      || (facadeAt(line, position.character) ? FACADE_DOC : '');
    return markdown ? hover(markdown) : undefined;
  },
};

const register = (context) => {
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(SELECTOR, completionProvider, '.'),
    vscode.languages.registerHoverProvider(SELECTOR, hoverProvider),
  );
  return { completionProvider, hoverProvider };
};

module.exports = { SELECTOR, completionProvider, hoverProvider, itemsFor, register,
  typescriptAnswers };

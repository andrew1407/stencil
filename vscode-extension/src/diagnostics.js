// Squiggles for a .stc buffer. Two sources of the same diagnostics: on save, the CLI's
// `--script-check` (the compiled core, so an editor never disagrees with a run); while typing,
// the in-process parser copies. Both land as vscode.Diagnostic.
import * as vscode from 'vscode';

import { CONFIG_SECTION, LANGUAGE_ID, SETTINGS } from './lib/ids.js';
import { cliFor } from './lib/spawn/cliLocator.js';
import { forget, programFor } from './lib/programCache.js';
import { sourceLines, unitSpan } from './lib/spans.js';
import { CHECK_LINE, fromProgram, parseCheckOutput, runCheck } from './lib/scriptCheck.js';

// Typing must not lex the buffer per character; this is the order of the editor's own idle.
const DEBOUNCE_MS = 200;
const RELATED_MESSAGE = 'the @use stencil this was expanded from';

/* Both sources count lines and byte columns from 1, the editor UTF-16 units from 0
 * (lib/spans.js), and a zero-length span would draw nothing. */
const rangeOf = (lines, { line, col, len }) => {
  const span = unitSpan(lines, { line: Math.max(1, line), col: Math.max(1, col), len });
  return new vscode.Range(span.line, span.start, span.line, Math.max(span.end, span.start + 1));
};

// One entry as a vscode.Diagnostic; its `related` span is placed in the document at `uri`.
const toDiagnostic = (entry, lines = [], uri = null) => {
  const severity = entry.severity === 'warning'
    ? vscode.DiagnosticSeverity.Warning
    : vscode.DiagnosticSeverity.Error;
  const diagnostic = new vscode.Diagnostic(rangeOf(lines, entry), entry.message, severity);
  diagnostic.source = 'stencil';
  if (entry.code) diagnostic.code = entry.code;
  if (entry.related && uri) {
    const at = new vscode.Location(uri, rangeOf(lines, entry.related));
    diagnostic.relatedInformation = [new vscode.DiagnosticRelatedInformation(at, RELATED_MESSAGE)];
  }
  return diagnostic;
};

const keyOf = (d) => `${d.code}:${d.line}:${d.col}:${d.message}`;

/* The calling `@use stencil` of a template-body diagnostic, which a `--script-check` line has no
 * field for: the parser copies' own diagnostic with the same code, span and message lends it. */
const relate = (entries, found) => {
  const calls = new Map(found.filter((d) => d.related).map((d) => [keyOf(d), d.related]));
  if (calls.size === 0) return entries;
  return entries.map((entry) => {
    const related = calls.get(keyOf(entry));
    return related ? { ...entry, related } : entry;
  });
};

const settings = () => vscode.workspace.getConfiguration(CONFIG_SECTION);

/* The diagnostics for one document. `saved` marks the on-disk path as current, which is what
 * lets the CLI read it; an unsaved buffer, a CLI switched off or silent, takes the copies. */
const collect = async (document, { saved }) => {
  let fromCli = null;
  if (saved && settings().get(SETTINGS.checkOnSave, true) && document.uri.scheme === 'file') {
    const cli = cliFor(vscode, document);
    fromCli = cli ? await runCheck(cli, document.uri.fsPath) : null;
  }
  const program = await programFor(document);
  return relate(fromCli ?? fromProgram(program), program.diagnostics);
};

const register = (context) => {
  const collection = vscode.languages.createDiagnosticCollection(LANGUAGE_ID);
  const timers = new Map();
  context.subscriptions.push(collection);

  // A collect() is asynchronous, so a result the next keystroke has already outdated is dropped.
  const refresh = async (document, options) => {
    if (!document || document.languageId !== LANGUAGE_ID) return;
    const { version } = document;
    const entries = await collect(document, options);
    if (document.version !== version) return;
    const lines = sourceLines(document.getText());
    collection.set(document.uri, entries.map((entry) => toDiagnostic(entry, lines, document.uri)));
  };

  const later = (document) => {
    const key = String(document.uri);
    clearTimeout(timers.get(key));
    const timer = setTimeout(() => {
      timers.delete(key);
      refresh(document, { saved: false });
    }, DEBOUNCE_MS);
    timer.unref?.();
    timers.set(key, timer);
  };

  const close = (document) => {
    clearTimeout(timers.get(String(document.uri)));
    timers.delete(String(document.uri));
    forget(document.uri);
    collection.delete(document.uri);
  };

  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument((d) => refresh(d, { saved: true })),
    vscode.workspace.onDidSaveTextDocument((d) => refresh(d, { saved: true })),
    vscode.workspace.onDidCloseTextDocument(close),
    vscode.workspace.onDidChangeTextDocument((e) => {
      if (settings().get(SETTINGS.checkOnType, true)) later(e.document);
    }),
  );
  for (const document of vscode.workspace.textDocuments) refresh(document, { saved: true });
  return collection;
};

export {
  CHECK_LINE, DEBOUNCE_MS, collect, fromProgram, parseCheckOutput, register, toDiagnostic,
};

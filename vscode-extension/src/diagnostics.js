// Squiggles for a .stc buffer. Two sources of the same diagnostics: on save, the CLI's
// `--script-check` (the compiled core, so an editor never disagrees with a run); while typing,
// the in-process parser copies. Both land as vscode.Diagnostic.
'use strict';

const vscode = require('vscode');

const { CONFIG_SECTION, LANGUAGE_ID, SETTINGS } = require('./lib/ids.js');
const { cliFor } = require('./lib/spawn/cliLocator.js');
const { forget, programFor } = require('./lib/programCache.js');
const { CHECK_LINE, fromProgram, parseCheckOutput, runCheck } = require('./lib/scriptCheck.js');

// Typing must not lex the buffer per character; this is the order of the editor's own idle.
const DEBOUNCE_MS = 200;

/* One entry as a vscode.Diagnostic. The parser counts lines and columns from 1, the editor
 * from 0, and a zero-length span would draw nothing. */
const toDiagnostic = (entry) => {
  const line = Math.max(0, entry.line - 1);
  const col = Math.max(0, entry.col - 1);
  const range = new vscode.Range(line, col, line, col + Math.max(1, entry.len));
  const severity = entry.severity === 'warning'
    ? vscode.DiagnosticSeverity.Warning
    : vscode.DiagnosticSeverity.Error;
  const diagnostic = new vscode.Diagnostic(range, entry.message, severity);
  diagnostic.source = 'stencil';
  if (entry.code) diagnostic.code = entry.code;
  return diagnostic;
};

const settings = () => vscode.workspace.getConfiguration(CONFIG_SECTION);

/* The diagnostics for one document. `saved` marks the on-disk path as current, which is what
 * lets the CLI read it; an unsaved buffer, a CLI switched off or silent, takes the copies. */
const collect = async (document, { saved }) => {
  if (saved && settings().get(SETTINGS.checkOnSave, true) && document.uri.scheme === 'file') {
    const cli = cliFor(vscode, document);
    const fromCli = cli ? await runCheck(cli, document.uri.fsPath) : null;
    if (fromCli) return fromCli;
  }
  return fromProgram(await programFor(document));
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
    if (document.version === version) collection.set(document.uri, entries.map(toDiagnostic));
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

module.exports = {
  CHECK_LINE, DEBOUNCE_MS, collect, fromProgram, parseCheckOutput, register, toDiagnostic,
};

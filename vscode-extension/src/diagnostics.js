// Squiggles for a .stc buffer. Two sources of the same diagnostics: on save, the CLI's
// `--script-check` (the compiled core, so an editor never disagrees with a run); while
// typing, the in-process parser copies. Both land as vscode.Diagnostic.
'use strict';

const { spawnSync } = require('node:child_process');
const { dirname } = require('node:path');
const vscode = require('vscode');

const { CONFIG_SECTION, LANGUAGE_ID, SETTINGS } = require('./lib/ids.js');
const { locateCli } = require('./lib/cliLocator.js');
const { loadParser } = require('./lib/parserHost.js');

// `{file}:{line}:{col}: {severity}: {message} [{CODE}]` — cli/src/script/load.zig writes it,
// this reads it. The file field is greedy so a Windows drive letter stays in it.
const CHECK_LINE = /^(.*):(\d+):(\d+): (error|warning): (.*?)(?: \[([A-Z_]+)\])?$/;

const parseCheckOutput = (text) => {
  const found = [];
  for (const raw of String(text ?? '').split('\n')) {
    const m = CHECK_LINE.exec(raw.trimEnd());
    if (!m) continue;
    found.push({
      line: Number(m[2]), col: Number(m[3]), len: 1,
      severity: m[4], message: m[5], code: m[6] ?? '',
    });
  }
  return found;
};

const fromProgram = (program) => program.diagnostics.map((d) => ({
  line: d.line, col: d.col, len: d.len || 1,
  severity: d.severity, message: d.message, code: d.code,
}));

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

const runCheck = (cli, path) => {
  const result = spawnSync(cli, ['--script-check', path], {
    cwd: dirname(path), encoding: 'utf8', shell: false,
  });
  if (result.error) return null;
  return parseCheckOutput(`${result.stdout ?? ''}${result.stderr ?? ''}`);
};

const settings = () => vscode.workspace.getConfiguration(CONFIG_SECTION);

const cliFor = (document) => locateCli({
  configured: settings().get(SETTINGS.cliPath, ''),
  baseDir: vscode.workspace.getWorkspaceFolder?.(document.uri)?.uri?.fsPath ?? '',
});

/* The diagnostics for one document. `saved` marks the on-disk path as current, which is
 * what lets the CLI read it; an unsaved buffer always goes to the parser copies. */
const collect = async (document, { saved }) => {
  if (saved && document.uri.scheme === 'file') {
    const cli = cliFor(document);
    const fromCli = cli ? runCheck(cli, document.uri.fsPath) : null;
    if (fromCli) return fromCli;
  }
  const { parseScript } = await loadParser();
  return fromProgram(parseScript(document.getText()));
};

const register = (context) => {
  const collection = vscode.languages.createDiagnosticCollection(LANGUAGE_ID);
  context.subscriptions.push(collection);

  const refresh = async (document, options) => {
    if (!document || document.languageId !== LANGUAGE_ID) return;
    collection.set(document.uri, (await collect(document, options)).map(toDiagnostic));
  };

  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument((d) => refresh(d, { saved: true })),
    vscode.workspace.onDidSaveTextDocument((d) => refresh(d, { saved: true })),
    vscode.workspace.onDidCloseTextDocument((d) => collection.delete(d.uri)),
    vscode.workspace.onDidChangeTextDocument((e) => {
      if (settings().get(SETTINGS.checkOnType, true)) refresh(e.document, { saved: false });
    }),
  );
  for (const document of vscode.workspace.textDocuments) refresh(document, { saved: true });
  return collection;
};

module.exports = { CHECK_LINE, collect, fromProgram, parseCheckOutput, register, toDiagnostic };

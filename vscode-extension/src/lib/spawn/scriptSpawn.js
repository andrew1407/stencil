// What every terminal command does before it runs anything: find the buffer, find the
// binary, save the file, and hand the argument list to the reused terminal. `vscode` comes
// in as an argument, so this stays testable without the editor.
'use strict';

const { dirname } = require('node:path');

const { runInTerminal } = require('./terminal.js');

// The active buffer, when it is one this command speaks for.
const activeIn = (vscode, accept) => {
  const editor = vscode.window.activeTextEditor;
  if (!editor || !accept(editor.document)) return null;
  return editor.document;
};

/* Saves first — every mode reads the file from disk — then runs `buildArgs(path)`. Returns
 * the terminal, or null when there is no such buffer, no binary, or no file behind it. */
const spawn = async (vscode, { document, missing, locate, missingBinary, buildArgs }) => {
  if (!document) {
    vscode.window.showErrorMessage(missing);
    return null;
  }
  const binary = locate(document);
  if (!binary) {
    vscode.window.showErrorMessage(missingBinary);
    return null;
  }
  // A cancelled save answers false; an untitled buffer's fsPath is a label, not a path.
  if (document.isDirty && !(await document.save())) return null;
  if (document.uri.scheme !== 'file') {
    vscode.window.showErrorMessage('Save the script to a file first');
    return null;
  }
  const path = document.uri.fsPath;
  const args = await buildArgs(path);
  if (!args) return null;
  return runInTerminal(vscode, { cli: binary, args, cwd: dirname(path) });
};

module.exports = { activeIn, spawn };

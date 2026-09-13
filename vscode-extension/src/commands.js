// The three commands, all of them one CLI invocation in the reused `Stencil` terminal with
// the script's own directory as the working directory, so a relative @source resolves the
// way it does on the command line.
'use strict';

const { dirname } = require('node:path');
const vscode = require('vscode');

const { COMMANDS, CONFIG_SECTION, LANGUAGE_ID, SETTINGS } = require('./lib/ids.js');
const { MISSING_CLI_MESSAGE, locateCli } = require('./lib/cliLocator.js');
const { runInTerminal } = require('./lib/terminal.js');

const activeScript = () => {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== LANGUAGE_ID) return null;
  return editor.document;
};

const cliFor = (document) => locateCli({
  configured: vscode.workspace.getConfiguration(CONFIG_SECTION).get(SETTINGS.cliPath, ''),
  baseDir: vscode.workspace.getWorkspaceFolder?.(document.uri)?.uri?.fsPath ?? '',
});

/* Saves first — every mode reads the file from disk — then runs `args(path)`. Returns the
 * terminal, or null when there is no .stc buffer or no CLI. */
const spawnFor = async (buildArgs) => {
  const document = activeScript();
  if (!document) {
    vscode.window.showErrorMessage('Open a .stc script first');
    return null;
  }
  const cli = cliFor(document);
  if (!cli) {
    vscode.window.showErrorMessage(MISSING_CLI_MESSAGE);
    return null;
  }
  if (document.isDirty) await document.save();
  const path = document.uri.fsPath;
  const args = await buildArgs(path);
  if (!args) return null;
  return runInTerminal(vscode, { cli, args, cwd: dirname(path) });
};

const pickImage = async () => {
  const picked = await vscode.window.showOpenDialog({
    canSelectMany: false, openLabel: 'Run script on', title: 'Stencil: image to run the script on',
  });
  return picked && picked.length ? picked[0].fsPath : null;
};

const runScript = () => spawnFor((path) => ['--script', path]);
const checkScript = () => spawnFor((path) => ['--script-check', path]);
const runScriptOnImage = () => spawnFor(async (path) => {
  const image = await pickImage();
  return image ? ['-i', image, '--script', path] : null;
});

const HANDLERS = {
  [COMMANDS.runScript]: runScript,
  [COMMANDS.checkScript]: checkScript,
  [COMMANDS.runScriptOnImage]: runScriptOnImage,
};

const register = (context) => {
  for (const [id, handler] of Object.entries(HANDLERS)) {
    context.subscriptions.push(vscode.commands.registerCommand(id, handler));
  }
  return HANDLERS;
};

module.exports = { HANDLERS, checkScript, register, runScript, runScriptOnImage };

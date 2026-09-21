// The terminal commands: the three `.stc` CLI invocations, emitting the script for another
// surface, and running an emitted `.pystc` on its interpreter. Each runs in the reused
// `Stencil` terminal with the script's own directory as the working directory, so a relative
// @source resolves the way it does on the command line.
'use strict';

const vscode = require('vscode');

const { COMMANDS, LANGUAGE_ID } = require('./lib/ids.js');
const { MISSING_CLI_MESSAGE, cliFor } = require('./lib/spawn/cliLocator.js');
const { MISSING_PYTHON_MESSAGE, pythonFor } = require('./lib/spawn/pythonLocator.js');
const { emitTarget, pickEmitTarget } = require('./lib/emit/targets.js');
const { isPySource } = require('./lib/emit/pySource.js');
const { activeIn, spawn } = require('./lib/spawn/scriptSpawn.js');

const spawnCli = (buildArgs) => spawn(vscode, {
  document: activeIn(vscode, (document) => document.languageId === LANGUAGE_ID),
  missing: 'Open a .stc script first',
  locate: (document) => cliFor(vscode, document),
  missingBinary: MISSING_CLI_MESSAGE,
  buildArgs,
});

const pickImage = async () => {
  const picked = await vscode.window.showOpenDialog({
    canSelectMany: false, openLabel: 'Run script on', title: 'Stencil: image to run the script on',
  });
  return picked && picked.length ? picked[0].fsPath : null;
};

const runScript = () => spawnCli((path) => ['--script', path]);
const checkScript = () => spawnCli((path) => ['--script-check', path]);
const runScriptOnImage = () => spawnCli(async (path) => {
  const image = await pickImage();
  return image ? ['-i', image, '--script', path] : null;
});
const emitScript = () => spawnCli(async (path) => {
  const extension = await pickEmitTarget(vscode);
  return extension ? ['--script', path, '--script-emit', emitTarget(path, extension)] : null;
});

const runPythonScript = () => spawn(vscode, {
  document: activeIn(vscode, isPySource),
  missing: 'Open a .pystc script first',
  locate: (document) => pythonFor(vscode, document),
  missingBinary: MISSING_PYTHON_MESSAGE,
  buildArgs: (path) => [path],
});

const HANDLERS = Object.freeze({
  [COMMANDS.runScript]: runScript,
  [COMMANDS.checkScript]: checkScript,
  [COMMANDS.runScriptOnImage]: runScriptOnImage,
  [COMMANDS.emitScript]: emitScript,
  [COMMANDS.runPythonScript]: runPythonScript,
});

const register = (context) => {
  for (const [id, handler] of Object.entries(HANDLERS)) {
    context.subscriptions.push(vscode.commands.registerCommand(id, handler));
  }
  return HANDLERS;
};

module.exports = { HANDLERS, checkScript, emitScript, register, runPythonScript, runScript,
  runScriptOnImage };

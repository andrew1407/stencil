// One command: put the facade's types in the workspace, so the editor's own JavaScript
// service types `stencil` rather than calling it `any`. Nothing else here decides anything —
// where the file goes and what it contains is lib/typingsFile.js.
'use strict';

const vscode = require('vscode');

const { COMMANDS } = require('./lib/ids.js');
const { TYPINGS_FILE, install } = require('./lib/emit/typingsFile.js');

const NO_FOLDER = 'Open a folder first — the types go beside the JavaScript that uses them';

const targetFolder = () => {
  const document = vscode.window.activeTextEditor?.document;
  const own = document && vscode.workspace.getWorkspaceFolder?.(document.uri)?.uri?.fsPath;
  return own || vscode.workspace.workspaceFolders?.[0]?.uri?.fsPath || '';
};

/* Written, then the language service is restarted: a types file it has already decided to
 * live without is not picked up until it looks again. */
const addTypings = async () => {
  const folder = targetFolder();
  if (!folder) return vscode.window.showErrorMessage(NO_FOLDER);
  let written;
  try {
    written = install(folder);
  } catch (err) {
    return vscode.window.showErrorMessage(`Could not write ${TYPINGS_FILE} — ${err?.message ?? err}`);
  }
  await vscode.commands.executeCommand('typescript.restartTsServer');
  const also = written.config ? ' and a jsconfig.json beside it' : '';
  vscode.window.showInformationMessage(`Wrote ${TYPINGS_FILE}${also}. Hovering \`stencil\` now names its type.`);
  return written;
};

const HANDLERS = Object.freeze({ [COMMANDS.addTypings]: addTypings });

const register = (context) => {
  for (const [id, handler] of Object.entries(HANDLERS)) {
    context.subscriptions.push(vscode.commands.registerCommand(id, handler));
  }
  return HANDLERS;
};

module.exports = { HANDLERS, NO_FOLDER, addTypings, register, targetFolder };

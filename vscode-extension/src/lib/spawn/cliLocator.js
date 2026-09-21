// Finds the Stencil CLI. The path is EXPLICIT USER CONFIGURATION — the setting, then
// STENCIL_CLI, then PATH — and is never read out of the document being edited.
'use strict';

const { isAbsolute, resolve } = require('node:path');

const { CONFIG_SECTION, SETTINGS } = require('../ids.js');
const { isExecutableFile, onPath } = require('../pathSearch.js');

// Relative to the workspace folder; null when it names no executable, so callers can say so.
const resolveConfigured = (configured, baseDir) => {
  if (!configured) return null;
  const path = isAbsolute(configured) || !baseDir ? configured : resolve(baseDir, configured);
  return isExecutableFile(path) ? path : null;
};

const locateCli = ({ configured = '', baseDir = '', env = process.env } = {}) =>
  resolveConfigured(configured, baseDir)
  ?? (env.STENCIL_CLI ? resolveConfigured(env.STENCIL_CLI, baseDir) : null)
  ?? onPath('stencil', env);

// The one way a feature asks for the binary: the setting read fresh, against the document's
// own workspace folder.
const cliFor = (vscode, document) => locateCli({
  configured: vscode.workspace.getConfiguration(CONFIG_SECTION).get(SETTINGS.cliPath, ''),
  baseDir: vscode.workspace.getWorkspaceFolder?.(document.uri)?.uri?.fsPath ?? '',
});

const MISSING_CLI_MESSAGE = 'Stencil CLI not found — set stencil.cliPath';

module.exports = {
  MISSING_CLI_MESSAGE, cliFor, isExecutableFile, locateCli, onPath, resolveConfigured,
};

// Finds the Python that runs a .pystc. The interpreter is EXPLICIT USER CONFIGURATION — the
// setting, then STENCIL_PYTHON, then `python3`/`python` on PATH — and is never read out of
// the document being edited. Twin of lib/cliLocator.js.
'use strict';

const { CONFIG_SECTION, SETTINGS } = require('./ids.js');
const { onPath } = require('./pathSearch.js');
const { resolveConfigured } = require('./cliLocator.js');

const NAMES = ['python3', 'python'];

const locatePython = ({ configured = '', baseDir = '', env = process.env } = {}) => {
  const named = resolveConfigured(configured, baseDir)
    ?? (env.STENCIL_PYTHON ? resolveConfigured(env.STENCIL_PYTHON, baseDir) : null);
  if (named) return named;
  for (const name of NAMES) {
    const found = onPath(name, env);
    if (found) return found;
  }
  return null;
};

const pythonFor = (vscode, document) => locatePython({
  configured: vscode.workspace.getConfiguration(CONFIG_SECTION).get(SETTINGS.pythonPath, ''),
  baseDir: vscode.workspace.getWorkspaceFolder?.(document.uri)?.uri?.fsPath ?? '',
});

const MISSING_PYTHON_MESSAGE = 'Python not found — set stencil.pythonPath';

module.exports = { MISSING_PYTHON_MESSAGE, NAMES, locatePython, pythonFor };

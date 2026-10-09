// Finds the Python that runs a .pystc. The interpreter is EXPLICIT USER CONFIGURATION — the
// setting, then STENCIL_PYTHON, then `python3`/`python` on PATH — and is never read out of
// the document being edited. Twin of lib/spawn/cliLocator.js.
import { CONFIG_SECTION, SETTINGS } from '../ids.js';
import { onPath } from '../pathSearch.js';
import { isRelative, resolveConfigured } from './cliLocator.js';

const NAMES = ['python3', 'python'];

// A relative setting or STENCIL_PYTHON is refused as cliLocator refuses one.
const locatePython = ({ configured = '', env = process.env } = {}) => {
  if (isRelative(configured) || (!configured && isRelative(env.STENCIL_PYTHON))) return null;
  const named = resolveConfigured(configured)
    ?? (env.STENCIL_PYTHON ? resolveConfigured(env.STENCIL_PYTHON) : null);
  if (named) return named;
  for (const name of NAMES) {
    const found = onPath(name, env);
    if (found) return found;
  }
  return null;
};

const pythonFor = (vscode) => locatePython({
  configured: vscode.workspace.getConfiguration(CONFIG_SECTION).get(SETTINGS.pythonPath, ''),
});

const MISSING_PYTHON_MESSAGE = 'Python not found — set stencil.pythonPath (an absolute path)';

export { MISSING_PYTHON_MESSAGE, NAMES, locatePython, pythonFor };

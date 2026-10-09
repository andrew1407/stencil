// Finds the Stencil CLI. The path is EXPLICIT USER CONFIGURATION — the setting, then
// STENCIL_CLI, then PATH — and is never read out of the document being edited.
import { isAbsolute } from 'node:path';

import { CONFIG_SECTION, SETTINGS } from '../ids.js';
import { isExecutableFile, onPath } from '../pathSearch.js';

// A relative value would resolve against the open workspace, letting the workspace pick the binary.
const isRelative = (configured) => !!configured && !isAbsolute(configured);

// Null when it names no executable or is relative, so callers can say so.
const resolveConfigured = (configured) => {
  if (!configured || isRelative(configured)) return null;
  return isExecutableFile(configured) ? configured : null;
};

// A relative setting (or, with none, a relative STENCIL_CLI) is refused outright, never skipped.
const locateCli = ({ configured = '', env = process.env } = {}) => {
  if (isRelative(configured) || (!configured && isRelative(env.STENCIL_CLI))) return null;
  return resolveConfigured(configured)
    ?? (env.STENCIL_CLI ? resolveConfigured(env.STENCIL_CLI) : null)
    ?? onPath('stencil', env);
};

const configuredCli = (vscode) => vscode.workspace.getConfiguration(CONFIG_SECTION).get(SETTINGS.cliPath, '');

// The one way a feature asks for the binary: the setting read fresh.
const cliFor = (vscode) => locateCli({ configured: configuredCli(vscode) });

const MISSING_CLI_MESSAGE = 'Stencil CLI not found — set stencil.cliPath';
const RELATIVE_CLI_MESSAGE = 'stencil.cliPath and STENCIL_CLI must be absolute paths — a relative one is refused';

const missingCliMessage = (vscode, env = process.env) => {
  const configured = configuredCli(vscode);
  return isRelative(configured) || (!configured && isRelative(env.STENCIL_CLI))
    ? RELATIVE_CLI_MESSAGE : MISSING_CLI_MESSAGE;
};

export {
  MISSING_CLI_MESSAGE, RELATIVE_CLI_MESSAGE, cliFor, isExecutableFile, isRelative, locateCli,
  missingCliMessage, onPath, resolveConfigured,
};

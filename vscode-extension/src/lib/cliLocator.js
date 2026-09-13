// Finds the Stencil CLI. The path is EXPLICIT USER CONFIGURATION — the setting, then
// STENCIL_CLI, then PATH — and is never read out of the document being edited. No shell is
// consulted: PATH is walked here, so nothing ever gets word-split or expanded.
'use strict';

const { accessSync, constants, statSync } = require('node:fs');
const { delimiter, isAbsolute, join, resolve } = require('node:path');

const EXE_SUFFIXES = process.platform === 'win32' ? ['.exe', '.cmd', '.bat', ''] : [''];

const isExecutableFile = (path) => {
  try {
    if (!statSync(path).isFile()) return false;
    accessSync(path, constants.X_OK);
    return true;
  } catch {
    return false;
  }
};

// Relative to the workspace folder; null when it names no executable, so callers can say so.
const resolveConfigured = (configured, baseDir) => {
  if (!configured) return null;
  const path = isAbsolute(configured) || !baseDir ? configured : resolve(baseDir, configured);
  return isExecutableFile(path) ? path : null;
};

// A name carrying a separator is a path, not a PATH lookup.
const onPath = (name, env) => {
  if (name.includes('/') || name.includes('\\')) return isExecutableFile(name) ? name : null;
  for (const dir of String(env.PATH || '').split(delimiter)) {
    if (!dir) continue;
    for (const suffix of EXE_SUFFIXES) {
      const candidate = join(dir, name + suffix);
      if (isExecutableFile(candidate)) return candidate;
    }
  }
  return null;
};

const locateCli = ({ configured = '', baseDir = '', env = process.env } = {}) =>
  resolveConfigured(configured, baseDir)
  ?? (env.STENCIL_CLI ? resolveConfigured(env.STENCIL_CLI, baseDir) : null)
  ?? onPath('stencil', env);

const MISSING_CLI_MESSAGE = 'Stencil CLI not found — set stencil.cliPath';

module.exports = { MISSING_CLI_MESSAGE, isExecutableFile, locateCli, onPath, resolveConfigured };

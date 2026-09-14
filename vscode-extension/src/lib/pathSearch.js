// What counts as an executable on disk, and the PATH walk that finds one. No shell is
// consulted, so nothing is word-split or expanded; the walk is memoized briefly, because an
// open-and-save burst would otherwise stat every PATH directory twice. A miss sweeps the dead.
'use strict';

const { accessSync, constants, statSync } = require('node:fs');
const { delimiter, join } = require('node:path');

const EXE_SUFFIXES = Object.freeze(
  process.platform === 'win32' ? ['.exe', '.cmd', '.bat', ''] : ['']);

const WALK_TTL_MS = 5_000;
const walked = new Map();

const isExecutableFile = (path) => {
  try {
    if (!statSync(path).isFile()) return false;
    accessSync(path, constants.X_OK);
    return true;
  } catch { return false; }
};

const walk = (name, env) => {
  for (const dir of String(env.PATH || '').split(delimiter)) {
    if (!dir) continue;
    for (const suffix of EXE_SUFFIXES) {
      const candidate = join(dir, name + suffix);
      if (isExecutableFile(candidate)) return candidate;
    }
  }
  return null;
};

// A name carrying a separator is a path, not a PATH lookup.
const onPath = (name, env) => {
  if (name.includes('/') || name.includes('\\')) return isExecutableFile(name) ? name : null;
  const key = `${name}\0${env.PATH ?? ''}`;
  const now = Date.now();
  const hit = walked.get(key);
  if (hit && now - hit.at < WALK_TTL_MS) return hit.path;
  for (const [stale, seen] of walked) if (now - seen.at >= WALK_TTL_MS) walked.delete(stale);
  const path = walk(name, env);
  walked.set(key, { at: Date.now(), path });
  return path;
};

const forgetPathWalk = () => walked.clear();

module.exports = { EXE_SUFFIXES, WALK_TTL_MS, forgetPathWalk, isExecutableFile, onPath };

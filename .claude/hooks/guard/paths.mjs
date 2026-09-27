// Path rules for the guard: where a path resolves, whether it leaves the repo or lands in a
// temp folder, and whether it names secret/credential material.

import os from 'node:os';
import path from 'node:path';

function toPosix(p) {
  return p.split(path.sep).join('/').replace(/\\/g, '/');
}

export function resolveAbs(p, ctx) {
  let s = String(p);
  if (s === '~' || s.startsWith('~/') || s.startsWith('~\\')) s = ctx.homeDir + s.slice(1);
  if (!path.isAbsolute(s)) s = path.resolve(ctx.repoRoot, s);
  return path.normalize(s);
}

export function isTmp(abs) {
  const p = toPosix(abs).toLowerCase();
  return p.startsWith('/tmp/') || p.startsWith('/private/tmp/') || p.startsWith('/var/folders/') ||
    p.startsWith(toPosix(os.tmpdir()).toLowerCase());
}

export function isOutsideRepo(p, ctx) {
  const abs = resolveAbs(p, ctx);
  const rel = path.relative(ctx.repoRoot, abs);
  return rel === '' ? false : (rel.startsWith('..') || path.isAbsolute(rel));
}

// `.env` and every `.env.<name>` (`.env.local`, `.env.production`) except the committed templates.
const ENV_FILE = /^\.env(\.(?!(example|sample|template|dist)$)[\w.-]+)?$/i;

// A file whose contents are secret/credential material. `secrets.json` is the desktop's
// owner-only connection-token store.
export function isSecretPath(abs) {
  const posix = toPosix(abs);
  const base = posix.split('/').pop() || '';
  if (ENV_FILE.test(base)) return true;
  if (/\.(pem|key|p12|pfx|jks|keystore)$/i.test(base)) return true;
  if (base === 'id_rsa' || base === 'id_ed25519' || base === 'id_dsa' || base === 'id_ecdsa') return true;
  if (base === 'openInConfig.json' || base === 'secrets.json') return true;
  if (/\/\.ssh\//.test(posix) || /\/\.aws\//.test(posix) || /\/\.config\/gcloud\//.test(posix) ||
      /\/\.gnupg\//.test(posix) || /\/\.docker\/config\.json$/.test(posix)) return true;
  if (/\/\.git\/config$/.test(posix)) return true;
  return false;
}

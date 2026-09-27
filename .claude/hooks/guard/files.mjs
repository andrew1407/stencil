// File-tool rules for the guard: Read/Edit/Write/NotebookEdit on a path, and Grep/Glob over a
// path or glob — a secret denies, a write outside the repo asks.

import { isOutsideRepo, isSecretPath, isTmp, resolveAbs } from './paths.mjs';
import { allow, ask, deny } from './verdict.mjs';

const expandBraces = (s) => {
  const m = /\{([^{}]*)\}/.exec(s);
  return m ? m[1].split(',').flatMap((alt) => expandBraces(s.slice(0, m.index) + alt + s.slice(m.index + m[0].length))) : [s];
};

// A glob touches a secret when one of its literal spellings does: `**/.env*` reads as `.env`.
function globTouchesSecret(glob, ctx) {
  return expandBraces(String(glob)).some((alt) => {
    const abs = resolveAbs(alt.replace(/\*\*\/?/g, '').replace(/[*?]/g, '') || '.', ctx);
    return isSecretPath(abs) || isSecretPath(`${abs}/x`);
  });
}

export function fileDecision(toolName, input, ctx) {
  const fp = input.file_path || input.path || input.notebook_path;
  if (!fp) return allow();
  const abs = resolveAbs(fp, ctx);
  if (isSecretPath(abs)) return deny('accesses a secret/credential file');
  if (toolName !== 'Read') {
    if (isOutsideRepo(fp, ctx) && !isTmp(abs)) return ask('writes to a path outside the repository');
  }
  return allow();
}

// Grep reads content under `path` filtered by `glob`; Glob lists what `pattern` matches under `path`.
export function searchDecision(toolName, input, ctx) {
  const globs = toolName === 'Grep' ? [input.glob] : [input.pattern];
  const hit = [input.path, ...globs].some((v) => typeof v === 'string' && v && globTouchesSecret(v, ctx));
  return hit ? deny('searches a secret/credential path') : allow();
}

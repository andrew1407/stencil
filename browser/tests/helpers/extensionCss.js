// The extension's two shared stylesheets are sheet SETS on disk (src/lib/animations/,
// src/lib/theme/). Read each set whole so a browser↔extension parity assertion never names
// one sheet of a set; every assertion here matches a declaration or a @keyframes block, so
// the concatenation order does not affect them.
import { readdirSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const setText = (dirRel) => {
  const dir = fileURLToPath(new URL(dirRel, import.meta.url));
  return readdirSync(dir).filter((f) => f.endsWith('.css')).sort()
    .map((f) => readFileSync(dir + f, 'utf8')).join('\n');
};

export const extensionAnimationsCss = () => setText('../../../browser-extension/src/lib/animations/');
export const extensionThemeCss = () => setText('../../../browser-extension/src/lib/theme/');

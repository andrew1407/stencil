// Browser↔desktop parity pins read the desktop's C++ for a shared constant. A desktop file
// splits into sibling TUs (`xParts.hpp`, `xStack.cpp`) as it grows, which moves the constant
// without moving the behaviour — so resolve these pins over the whole family, by basename
// prefix, never against one file.
import { readdirSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, basename } from 'node:path';

export const desktopSource = (relPrefix) => {
  const abs = fileURLToPath(new URL(`../../../desktop/src/${relPrefix}`, import.meta.url));
  const [dir, prefix] = [dirname(abs), basename(abs)];
  const files = readdirSync(dir).filter((f) => f.startsWith(prefix) && /\.(cpp|hpp)$/.test(f));
  if (!files.length) throw new Error(`no desktop source matches ${relPrefix}`);
  return files.sort().map((f) => readFileSync(`${dir}/${f}`, 'utf8')).join('\n');
};

// The size budget every capture script checks before it exits, so a heavy shot is re-taken
// with a clip instead of landing in git.
import fs from 'node:fs';
import path from 'node:path';

const maxFor = (file, budget) => (file.endsWith('.gif') ? budget.gifMaxBytes
  : file.endsWith('.png') ? budget.pngMaxBytes : Infinity);

const kb = (bytes) => Math.round(bytes / 1024);

export function checkBudget(dir, budget) {
  const over = fs.readdirSync(dir)
    .map((file) => ({ file, size: fs.statSync(path.join(dir, file)).size }))
    .filter(({ file, size }) => size > maxFor(file, budget))
    .map(({ file, size }) => `${file} ${kb(size)} KB > ${kb(maxFor(file, budget))} KB`);
  if (over.length) throw new Error(`over the image budget:\n  ${over.join('\n  ')}`);
}

export const sizesTable = (dir) => fs.readdirSync(dir).sort()
  .map((file) => `${String(kb(fs.statSync(path.join(dir, file)).size)).padStart(6)} KB  ${file}`).join('\n');

// The size budget every capture script checks before it exits, so a heavy shot is re-taken
// with a clip instead of landing in git.
import { readdir, stat } from 'node:fs/promises';
import path from 'node:path';

const maxFor = (file, budget) => (file.endsWith('.gif') ? budget.gifMaxBytes
  : file.endsWith('.png') ? budget.pngMaxBytes : Infinity);

const kb = (bytes) => Math.round(bytes / 1024);

// Every file in `dir` with its size in bytes, by name.
export const sizesOf = async (dir) => {
  const files = (await readdir(dir)).sort();
  return Promise.all(files.map(async (file) => ({ file, size: (await stat(path.join(dir, file))).size })));
};

export function checkBudget(sizes, budget) {
  const over = sizes
    .filter(({ file, size }) => size > maxFor(file, budget))
    .map(({ file, size }) => `${file} ${kb(size)} KB > ${kb(maxFor(file, budget))} KB`);
  if (over.length) throw new Error(`over the image budget:\n  ${over.join('\n  ')}`);
}

export const sizesTable = (sizes) => sizes
  .map(({ file, size }) => `${String(kb(size)).padStart(6)} KB  ${file}`).join('\n');

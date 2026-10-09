// The repo as the tools read it, asynchronously: one git call for the file list, one
// `git cat-file --batch` for every blob at a ref, and file reads in bounded parallel batches.

import { execFile, spawn } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { promisify } from 'node:util';

const execFileP = promisify(execFile);
// Open files at once: well under macOS's default 256 descriptors.
export const READ_LIMIT = 64;

export const git = async (root, ...args) =>
  (await execFileP('git', ['-C', root, ...args], { encoding: 'utf8', maxBuffer: 256 << 20 })).stdout;

export const repoRoot = async (from = process.cwd()) => (await git(from, 'rev-parse', '--show-toplevel')).trim();

// `fn` over every item, at most `limit` in flight, results in input order.
export const mapLimit = async (items, fn, limit = READ_LIMIT) => {
  const out = new Array(items.length);
  let next = 0;
  const worker = async () => { for (let i = next++; i < items.length; i = next++) out[i] = await fn(items[i], i); };
  await Promise.all(Array.from({ length: Math.min(limit, items.length) }, worker));
  return out;
};

// Whether `f` is one of `only`, or a file below one of them.
export const under = (f, only) => only.some((o) => f === o || f.startsWith(`${o.replace(/\/$/, '')}/`));

// Tracked and would-be-tracked files on disk (gitignored output never), optionally under `only`.
export const listFiles = async (root, only = [], pathspec = []) => {
  const [all, deleted] = await Promise.all([
    git(root, 'ls-files', '-co', '--exclude-standard', '--', ...pathspec),
    git(root, 'ls-files', '-d', '--', ...pathspec),
  ]);
  const gone = new Set(deleted.split('\n'));
  const files = [...new Set(all.split('\n'))].filter((f) => f && !gone.has(f));
  return only.length ? files.filter((f) => under(f, only)) : files;
};

export const readText = (root, file) => readFile(path.join(root, file), 'utf8');
export const readAll = (root, files) => mapLimit(files, (f) => readText(root, f));

// Each file's text at `ref`, or null where the ref has no such blob, from one git process.
export const blobsAt = (root, ref, files) => new Promise((resolve, reject) => {
  const child = spawn('git', ['-C', root, 'cat-file', '--batch'], { stdio: ['pipe', 'pipe', 'ignore'] });
  const chunks = [];
  child.stdout.on('data', (c) => chunks.push(c));
  child.on('error', reject);
  child.on('close', (code) => {
    if (code !== 0) { reject(new Error(`git cat-file exited ${code}`)); return; }
    const buf = Buffer.concat(chunks);
    const out = new Map();
    let at = 0;
    for (const f of files) {
      const eol = buf.indexOf(0x0a, at);
      const head = /^[0-9a-f]{40,64} (\w+) (\d+)$/.exec(buf.toString('utf8', at, eol));
      at = eol + 1;
      if (!head) { out.set(f, null); continue; }   // `<name> missing`
      const size = Number(head[2]);
      out.set(f, head[1] === 'blob' ? buf.toString('utf8', at, at + size) : null);
      at += size + 1;   // the object, then its newline
    }
    resolve(out);
  });
  child.stdin.end(files.map((f) => `${ref}:${f}\n`).join(''));
});

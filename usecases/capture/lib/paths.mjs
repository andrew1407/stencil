// Where every capture script reads from and writes to. Outputs are the committed images
// under usecases/<app>/img/; intermediates go to a gitignored scratch dir beside the scripts.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const HERE = path.dirname(fileURLToPath(import.meta.url));
export const CAPTURE = path.dirname(HERE);
export const DOCS = path.dirname(CAPTURE);
export const REPO = path.dirname(DOCS);
export const CONFIG_DIR = path.join(CAPTURE, 'config');
export const SCRATCH = process.env.STENCIL_CAPTURE_SCRATCH || path.join(CAPTURE, '.out');

export const expandHome = (p) => (p.startsWith('~') ? path.join(os.homedir(), p.slice(1)) : p);

export const repoPath = (...parts) => path.join(REPO, ...parts);

export const outDir = (app) => {
  const dir = path.join(DOCS, app, 'img');
  fs.mkdirSync(dir, { recursive: true });
  return dir;
};

// A fresh intermediate directory: frames and videos are rebuilt on every run.
export const scratchDir = (name) => {
  const dir = path.join(SCRATCH, name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  return dir;
};

export const scratchPath = (...parts) => path.join(SCRATCH, ...parts);

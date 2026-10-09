// The tree's size rules (.claude/rules/architecture.md) as one check: a source, test or stylesheet
// file stays at or under 230 lines; a non-test folder holds at most 12 direct source units; and a
// directory's body comments, doc banners aside, stay under a fifth of its lines.
//   node tools/caps.mjs [--check] [path...]   (--check exits 1 on a breach)

import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { commentsOf, langFor } from './commentPaths.mjs';
import { BUILD_OUTPUT } from './docPaths.mjs';

export const MAX_LINES = 230;
export const MAX_UNITS = 12;
export const MAX_COMMENT_SHARE = 0.2;

const SOURCE = /\.(m?js|cjs|ts|cpp|cc|hpp|h|c|mm|zig|py|go|rs|cs|css|qss)$/i;
const CODE = /\.(m?js|cjs|ts|cpp|cc|hpp|h|c|mm|zig|py|go|rs|cs)$/i;
// Written by a tool or fetched, never edited by hand.
const EXEMPT = /(^|\/)(vendor|third_party|zig-pkg|generated)\/|(^|\/)stb_\w+\.h$|^vscode-extension\/typings\//;
const TEST_DIR = /(^|\/)(tests?|__tests__|testdata)(\/|$)/;
const HEADER = /\.(hpp|h)$/i;
const BODY = /\.(cpp|cc|c|mm)$/i;
const PLATFORM = /(Mac|Win|Linux)$/;

export const lineCount = (text) => (text.length === 0 ? 0 : text.split('\n').length - (text.endsWith('\n') ? 1 : 0));

export const isSource = (file) => SOURCE.test(file) && !BUILD_OUTPUT.test(file) && !EXEMPT.test(file);

// A module and its .d.ts are one unit, as are a header, its .cpp and a per-OS body (`xMac.mm`).
export const unitOf = (file, headers) => {
  const base = path.posix.basename(file);
  const stem = base.endsWith('.d.ts') ? base.slice(0, -5) : base.replace(/\.[^.]+$/, '');
  const bare = stem.replace(PLATFORM, '');
  return BODY.test(base) && bare !== stem && headers.has(bare) ? bare : stem;
};

export const folderUnits = (files) => {
  const byDir = new Map();
  for (const f of files) {
    const dir = path.posix.dirname(f);
    if (!CODE.test(f) || TEST_DIR.test(dir) || f.endsWith('_test.go')) continue;
    if (!byDir.has(dir)) byDir.set(dir, []);
    byDir.get(dir).push(f);
  }
  return [...byDir].map(([dir, list]) => {
    const headers = new Set(list.filter((f) => HEADER.test(f)).map((f) => path.posix.basename(f).replace(HEADER, '')));
    return { dir, units: new Set(list.map((f) => unitOf(f, headers))).size };
  });
};

// Body comment lines: comment-only lines after the leading banner, which ends at the first code line.
export const commentLines = (src, lang) => {
  const inComment = new Uint8Array(src.length);
  for (const s of commentsOf(src, lang)) inComment.fill(1, s.start, s.end);
  let body = 0, banner = true, pos = 0;
  for (const line of src.split('\n')) {
    let code = false, comment = false;
    for (let i = 0; i < line.length; i++) {
      if (/\s/.test(line[i])) continue;
      if (inComment[pos + i]) comment = true; else code = true;
    }
    if (code) banner = false;
    else if (comment && !banner) body++;
    pos += line.length + 1;
  }
  return { body, lines: lineCount(src) };
};

export const breaches = (root, files) => {
  const sources = files.filter(isSource);
  const out = [];
  const share = new Map();
  for (const f of sources) {
    const src = readFileSync(path.join(root, f), 'utf8');
    const n = lineCount(src);
    if (n > MAX_LINES) out.push({ kind: 'lines', where: f, value: n, limit: MAX_LINES });
    const lang = langFor(f);
    if (!lang || f.endsWith('.d.ts')) continue;
    const { body, lines } = commentLines(src, lang);
    const dir = path.posix.dirname(f);
    const acc = share.get(dir) || { body: 0, lines: 0 };
    share.set(dir, { body: acc.body + body, lines: acc.lines + lines });
  }
  for (const { dir, units } of folderUnits(sources)) {
    if (units > MAX_UNITS) out.push({ kind: 'units', where: dir, value: units, limit: MAX_UNITS });
  }
  for (const [dir, { body, lines }] of share) {
    if (lines && body / lines >= MAX_COMMENT_SHARE) out.push({ kind: 'comments', where: dir, value: `${body}/${lines}`, limit: 'a fifth' });
  }
  return out;
};

const git = (root, ...args) => execFileSync('git', ['-C', root, ...args], { encoding: 'utf8', maxBuffer: 64 << 20 });

export const scanTree = (root, only = []) => {
  const files = git(root, 'ls-files', '-co', '--exclude-standard').split('\n')
    .filter((f) => f && existsSync(path.join(root, f)));
  const within = only.length ? files.filter((f) => only.some((o) => f === o || f.startsWith(`${o.replace(/\/$/, '')}/`))) : files;
  return breaches(root, within);
};

if (import.meta.url === pathToFileURL(process.argv[1] || '').href) {
  const args = process.argv.slice(2);
  if (args.some((a) => a.startsWith('-') && a !== '--check')) {
    console.error('usage: node tools/caps.mjs [--check] [path...]');
    process.exit(2);
  }
  const root = git(process.cwd(), 'rev-parse', '--show-toplevel').trim();
  const only = args.filter((a) => a !== '--check').map((a) => path.relative(root, path.resolve(a)));
  const found = scanTree(root, only);
  for (const b of found) console.log(`${b.where}  ${b.kind} ${b.value} (limit ${b.limit})`);
  console.log(`caps: ${found.length} breach${found.length === 1 ? '' : 'es'}`);
  process.exit(args.includes('--check') && found.length ? 1 : 0);
}

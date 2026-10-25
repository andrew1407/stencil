// The tree's size rules (.claude/rules/architecture.md) as one check: a source, test or stylesheet
// file stays at or under 230 lines; a function at or under 80 beyond tools/functionCaps.json; a
// non-test folder holds at most 12 direct source units; no file is named for a kind beyond
// tools/kindNames.json; and a directory's body comments, doc banners aside, stay under a fifth of its lines.
//   node tools/caps.mjs [--check] [path...]   (--check exits 1 on a breach)

import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { commentsOf } from './commentPaths.mjs';
import { langFor } from './moveCheck.mjs';
import { BUILD_OUTPUT } from './docPaths.mjs';
import { listFiles, readAll, repoRoot } from './repo/io.mjs';
import { MAX_FUNCTION_LINES, overCap } from './repo/functions.mjs';
import FUNCTION_CAPS from './functionCaps.json' with { type: 'json' };
import KIND_NAMES from './kindNames.json' with { type: 'json' };

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
// A split by kind, spelled in the file name: `fooHelpers.hpp`, `shared.js`, `layout-helpers.test.js`.
export const KIND_NAME = /(?:^|[a-z0-9_-])(?:helpers|shared|common|misc|utils)$/i;

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

// A function over the cap is a breach unless tools/functionCaps.json froze it, and then only once it
// grows past its entry; an entry no longer over the cap is a breach too, so the list only shrinks.
export const functionBreaches = (files, texts, frozen = FUNCTION_CAPS.functions) => {
  const out = [];
  const seen = new Set();
  files.forEach((f, i) => {
    if (TEST_DIR.test(path.posix.dirname(f)) || f.endsWith('.d.ts') || f.endsWith('_test.go')) return;
    for (const { key, lines } of overCap(f, texts[i])) {
      seen.add(key);
      if (!(key in frozen)) out.push({ kind: 'function', where: key, value: lines, limit: MAX_FUNCTION_LINES });
      else if (lines > frozen[key]) out.push({ kind: 'function', where: key, value: lines, limit: `its frozen ${frozen[key]}` });
    }
  });
  const read = new Set(files);
  for (const key of Object.keys(frozen)) {
    if (read.has(key.slice(0, key.indexOf('#'))) && !seen.has(key)) out.push({ kind: 'function', where: key, value: 'under the cap', limit: 'drop its tools/functionCaps.json entry' });
  }
  return out;
};

export const kindNamed = (file) => KIND_NAME.test(path.posix.basename(file).replace(/(\.(test|d|gui|headless))?\.[^.]+$/, ''));

// Same ratchet as the function cap: a frozen file may stay, a new one may not, a gone one leaves the list.
export const kindNameBreaches = (files, frozen = KIND_NAMES.files) => {
  const listed = new Set(frozen);
  const out = files.filter((f) => !f.endsWith('.d.ts') && kindNamed(f) && !listed.has(f))
    .map((f) => ({ kind: 'name', where: f, value: 'a kind name', limit: 'name it for its concern' }));
  const read = new Set(files);
  for (const f of frozen) {
    if (read.has(f) && !kindNamed(f)) out.push({ kind: 'name', where: f, value: 'no longer a kind name', limit: 'drop its tools/kindNames.json entry' });
  }
  return out;
};

export const breaches = async (root, files) => {
  const sources = files.filter(isSource);
  const texts = await readAll(root, sources);
  const out = [];
  const share = new Map();
  for (const [i, f] of sources.entries()) {
    const src = texts[i];
    const n = lineCount(src);
    if (n > MAX_LINES) out.push({ kind: 'lines', where: f, value: n, limit: MAX_LINES });
    const lang = langFor(f);
    if (!lang || f.endsWith('.d.ts')) continue;
    const { body, lines } = commentLines(src, lang);
    const dir = path.posix.dirname(f);
    const acc = share.get(dir) || { body: 0, lines: 0 };
    share.set(dir, { body: acc.body + body, lines: acc.lines + lines });
  }
  out.push(...functionBreaches(sources, texts));
  out.push(...kindNameBreaches(sources));
  for (const { dir, units } of folderUnits(sources)) {
    if (units > MAX_UNITS) out.push({ kind: 'units', where: dir, value: units, limit: MAX_UNITS });
  }
  for (const [dir, { body, lines }] of share) {
    if (lines && body / lines >= MAX_COMMENT_SHARE) out.push({ kind: 'comments', where: dir, value: `${body}/${lines}`, limit: 'a fifth' });
  }
  return out;
};

export const scanTree = async (root, only = []) => breaches(root, await listFiles(root, only));

if (import.meta.url === pathToFileURL(process.argv[1] || '').href) {
  const args = process.argv.slice(2);
  if (args.some((a) => a.startsWith('-') && a !== '--check')) {
    console.error('usage: node tools/caps.mjs [--check] [path...]');
    process.exit(2);
  }
  const root = await repoRoot();
  const only = args.filter((a) => a !== '--check').map((a) => path.relative(root, path.resolve(a)));
  const found = await scanTree(root, only);
  for (const b of found) console.log(`${b.where}  ${b.kind} ${b.value} (limit ${b.limit})`);
  console.log(`caps: ${found.length} breach${found.length === 1 ? '' : 'es'}`);
  process.exit(args.includes('--check') && found.length ? 1 : 0);
}

// ── syncTwins: re-copy every byte-equal port and drift-tested copy from its original ────
// Driven by twins.json. Writes nothing under --check; the surfaces' parity tests stay
// the enforcement, this is the one command that makes them green again after an edit.
//   node tools/syncTwins.mjs [--check] [path...]   (paths narrow to the rows touching them)
import { readdir, readFile, rm, writeFile } from 'node:fs/promises';
import { pathToFileURL, fileURLToPath } from 'node:url';
import path from 'node:path';
import { declaration, declarationShape, normalizePort, placed, regeneratePort } from './syncTwins/ports.mjs';
import { mapLimit } from './repo/io.mjs';

const ROOT = fileURLToPath(new URL('..', import.meta.url));
const PORT_TEST = 'browser-extension/tests/portParity.test.js';
const FUNCTIONS_TEST = 'browser-extension/tests/portParityFunctions.test.js';
// Rows judged together; a tree row alone reads both sides of every file it holds.
const ROWS_AT_ONCE = 8;

export const loadManifest = async (root = ROOT) => JSON.parse(await readFile(path.join(root, 'tools/twins.json'), 'utf8'));

export { declaration, declarationShape, normalizePort, regeneratePort } from './syncTwins/ports.mjs';

// ── rows → work items ───────────────────────────────────────────
const jsFiles = async (dir) => {
  const entries = (await readdir(dir, { withFileTypes: true })).sort((a, b) => (a.name < b.name ? -1 : 1));
  return (await Promise.all(entries.map((e) => (e.isDirectory() ? jsFiles(path.join(dir, e.name))
    : e.name.endsWith('.js') ? [path.join(dir, e.name)] : [])))).flat();
};
const filesOf = (abs) => (abs.endsWith('/') ? jsFiles(abs) : [abs]);
const read = (abs) => readFile(abs, 'utf8');
// A missing file reads as null.
const readOr = (abs) => read(abs).catch((err) => { if (err.code === 'ENOENT') return null; throw err; });
const listTree = async (abs, ext) => (await readdir(abs).catch(() => [])).filter((f) => ext.some((e) => f.endsWith(e))).sort();

// Each item: { label, test, from, to, async drift(): string[] (empty when in step), async sync(): string[] }.
export const itemsOf = (m, root = ROOT) => {
  const at = (rel) => path.join(root, rel);
  const bytesItem = (label, test, from, to, want) => ({
    label, test, from, to,
    drift: async () => {
      const [mine, theirs] = await Promise.all([readOr(at(to)), want()]);
      return mine === null ? ['missing'] : mine === theirs ? [] : ['bytes differ'];
    },
    sync: async () => { await writeFile(at(to), await want()); return [`wrote ${to}`]; },
  });
  const items = [];
  for (const r of m.copies) items.push(bytesItem(`copy ${r.to}`, r.test, r.from, r.to, () => read(at(r.from))));
  // Written by a build step (`by`) and gitignored: absent is fine, present must match.
  for (const r of m.generated ?? []) {
    const item = bytesItem(`generated ${r.to}`, r.test, r.from, r.to, () => read(at(r.from)));
    items.push({ ...item, drift: async () => ((await readOr(at(r.to))) === null ? [] : item.drift()) });
  }
  for (const r of m.picks) {
    const want = async () => { const c = JSON.parse(await read(at(r.from))); return `${JSON.stringify(Object.fromEntries(r.keys.map((k) => [k, c[k]])), null, 2)}\n`; };
    items.push(bytesItem(`pick ${r.to}`, r.test, r.from, r.to, want));
  }
  for (const r of m.trees) {
    const names = async () => {
      const [src, dst] = await Promise.all([listTree(at(r.from), r.ext), listTree(at(r.to), r.ext)]);
      return { src, dst };
    };
    // A tree copy equals its original but for the import specifiers `rewrite` declares.
    const want = async (f) => Object.entries(r.rewrite ?? {}).reduce((t, [a, b]) => t.split(a).join(b), await read(at(r.from + f)));
    // Each original file whose copy differs from what it should be (absent included).
    const stale = async (src) => (await Promise.all(src.map(async (f) => {
      const [w, mine] = await Promise.all([want(f), readOr(at(r.to + f))]);
      return w === mine ? null : [f, w];
    }))).filter(Boolean);
    items.push({
      label: `tree ${r.to}`, test: r.test, from: r.from, to: r.to,
      drift: async () => {
        const { src, dst } = await names();
        const differs = new Set((await stale(src.filter((f) => dst.includes(f)))).map(([f]) => f));
        return [...src.filter((f) => !dst.includes(f)).map((f) => `missing ${f}`),
          ...dst.filter((f) => !src.includes(f)).map((f) => `orphan ${f}`),
          ...src.filter((f) => differs.has(f)).map((f) => `${f} differs`)];
      },
      sync: async () => {
        const { src, dst } = await names();
        const done = [];
        for (const [f, w] of await stale(src)) { await writeFile(at(r.to + f), w); done.push(`wrote ${r.to}${f}`); }
        for (const f of dst.filter((x) => !src.includes(x))) { await rm(at(r.to + f)); done.push(`removed ${r.to}${f}`); }
        return done;
      },
    });
  }
  for (const r of m.ports) {
    items.push({
      label: `port ${r.name}`, test: r.test ?? PORT_TEST, from: r.from, to: r.to,
      drift: async () => {
        const [from, to] = await Promise.all([read(at(r.from)), read(at(r.to))]);
        return normalizePort(from) === normalizePort(to) ? [] : ['body differs'];
      },
      sync: async () => {
        const { text, missing } = regeneratePort(...await Promise.all([read(at(r.from)), read(at(r.to))]));
        if (missing.length) return [`SKIPPED ${r.to}: no spelling here for ${missing.join(', ')} — edit by hand`];
        await writeFile(at(r.to), text); return [`wrote ${r.to}`];
      },
    });
  }
  for (const r of m.functions) {
    // Both sides read once per pass: the original joined, the copy file by file.
    const sides = async () => {
      const [fromFiles, toFiles] = await Promise.all([filesOf(at(r.from)), filesOf(at(r.to))]);
      const [fromTexts, toTexts] = await Promise.all([Promise.all(fromFiles.map(read)), Promise.all(toFiles.map(read))]);
      const src = fromTexts.join('\n');
      return {
        theirs: (fn) => declaration(src, `${fn}JS`)?.replace(`${fn}JS`, fn) ?? declaration(src, fn),
        home: (fn) => { const i = toTexts.findIndex((t) => declaration(t, fn)); return i < 0 ? null : { file: toFiles[i], text: toTexts[i] }; },
      };
    };
    items.push({
      label: `functions ${r.name}`, test: r.test ?? FUNCTIONS_TEST, from: r.from, to: r.to,
      drift: async () => {
        const { theirs, home } = await sides();
        return r.names.filter((fn) => {
          const h = home(fn); return !h || declarationShape(declaration(h.text, fn)) !== declarationShape(theirs(fn)); })
          .map((fn) => `${fn} differs`);
      },
      sync: async () => {
        const done = [];
        for (const fn of r.names) {   // in turn: two functions may share a file
          const { theirs, home } = await sides();
          const h = home(fn); const want = theirs(fn);
          if (!h || !want) { done.push(`SKIPPED ${r.name}.${fn}: not found on both sides — edit by hand`); continue; }
          const mine = declaration(h.text, fn);
          if (declarationShape(mine) === declarationShape(want)) continue;
          await writeFile(h.file, h.text.replace(mine, () => placed(want, mine))); done.push(`wrote ${path.relative(root, h.file)} (${fn})`);
        }
        return done;
      },
    });
  }
  return items;
};

// ── CLI ─────────────────────────────────────────────────────────
const touches = (item, filters) => !filters.length
  || filters.some((f) => [item.from, item.to].some((p) => p.startsWith(f) || f.startsWith(p)));

// The PostToolUse note for one edited file: null when it is no twin's side or every pair is in step.
export const hookNote = async (payload, root = ROOT) => {
  const file = payload?.tool_input?.file_path;
  if (typeof file !== 'string' || !file) return null;
  const at = path.relative(root, path.resolve(root, file));
  if (at.startsWith('..')) return null;
  const touching = itemsOf(await loadManifest(root), root).filter((i) => touches(i, [at]));
  const drifts = await mapLimit(touching, (i) => i.drift(), ROWS_AT_ONCE);
  const drifted = touching.filter((_, k) => drifts[k].length);
  if (!drifted.length) return null;
  const copy = drifted.find((i) => at.startsWith(i.to));
  return copy
    ? `${at} is a copy of ${copy.from} — edit the original, then run node tools/syncTwins.mjs`
    : `${at} has twins out of step (${drifted.map((i) => i.to).join(', ')}) — run node tools/syncTwins.mjs`;
};

if (import.meta.url === pathToFileURL(process.argv[1] || '').href && process.argv[2] === '--hook') {
  let input = '';
  process.stdin.on('data', (c) => { input += c; });
  process.stdin.on('end', async () => {
    let note = null;
    try { note = await hookNote(JSON.parse(input)); } catch { note = null; }
    if (note) process.stderr.write(`${note}\n`);
    process.exit(note ? 2 : 0);
  });
} else if (import.meta.url === pathToFileURL(process.argv[1] || '').href) {
  const args = process.argv.slice(2);
  const check = args.includes('--check');
  const filters = args.filter((a) => a !== '--check').map((a) => path.relative(ROOT, path.resolve(a)));
  let drifted = 0;
  const items = itemsOf(await loadManifest()).filter((i) => touches(i, filters));
  // A check judges every row at once; a sync goes row by row, as two rows may share a file.
  const drifts = check ? await mapLimit(items, (i) => i.drift(), ROWS_AT_ONCE) : [];
  for (const [k, item] of items.entries()) {
    const drift = check ? drifts[k] : await item.drift();
    if (!drift.length) continue;
    drifted++;
    if (check) console.log(`DRIFT ${item.label}  (from ${item.from}; pinned by ${item.test})\n  ${drift.join('\n  ')}`);
    else for (const line of await item.sync()) console.log(line);
  }
  console.log(`syncTwins: ${drifted} pair${drifted === 1 ? '' : 's'} ${check ? 'drifted' : 're-synced'}`);
  process.exit(check && drifted ? 1 : 0);
}

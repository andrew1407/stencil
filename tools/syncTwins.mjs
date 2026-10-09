// ── syncTwins: re-copy every byte-equal port and drift-tested copy from its original ────
// Driven by twins.json. Writes nothing under --check; the surfaces' parity tests stay
// the enforcement, this is the one command that makes them green again after an edit.
//   node tools/syncTwins.mjs [--check] [path...]   (paths narrow to the rows touching them)
import { existsSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { pathToFileURL, fileURLToPath } from 'node:url';
import path from 'node:path';

const ROOT = fileURLToPath(new URL('..', import.meta.url));
const PORT_TEST = 'browser-extension/tests/portParity.test.js';
const FUNCTIONS_TEST = 'browser-extension/tests/portParityFunctions.test.js';

export const loadManifest = (root = ROOT) => JSON.parse(readFileSync(path.join(root, 'tools/twins.json'), 'utf8'));

// ── the port rules, as browser-extension/tests/portParity.test.js states them ──
// The header is the leading run of `//` lines and blanks; only it and import specifiers may differ.
const headerLength = (lines) => {
  let i = 0;
  while (i < lines.length && (/^\s*\/\//.test(lines[i]) || lines[i].trim() === '')) i++;
  return i;
};
const stripHeader = (src) => { const l = src.split('\n'); return l.slice(headerLength(l)).join('\n'); };
const SPEC = /(from\s+['"])([^'"]+)(['"])/g;
export const normalizePort = (src) => stripHeader(src).replace(SPEC, (_, a, s, b) => a + s.split('/').pop() + b);

// The copy keeps its own header and its own spelling of each import, found by basename.
export const regeneratePort = (original, copy) => {
  const lines = copy.split('\n');
  const spelled = new Map([...copy.matchAll(SPEC)].map((m) => [m[2].split('/').pop(), m[2]]));
  const missing = [];
  const body = stripHeader(original).replace(SPEC, (all, a, s, b) => {
    const mine = spelled.get(s.split('/').pop());
    if (!mine) missing.push(s);
    return mine ? a + mine + b : all;
  });
  return { text: [...lines.slice(0, headerLength(lines)), body].join('\n'), missing };
};

// A `/` after one of these opens a regex literal (a quote or bracket inside it is not structure).
const REGEX_AFTER = /(?:[(,=:[!&|?{};+\-*%<>~^]|\breturn|\btypeof)$/;
const regexEnd = (line, from) => {
  let cls = false;
  for (let j = from + 1; j < line.length; j++) {
    const c = line[j];
    if (c === '\\') j++;
    else if (cls) { if (c === ']') cls = false; } else if (c === '[') cls = true; else if (c === '/') return j;
  }
  return line.length;
};

// One `const NAME` / `function NAME` statement, to the line that balances it: the top-level one,
// else the first indented one (an inline copy inside an injected function or an IIFE).
export const declaration = (src, name) => {
  const lines = src.split('\n');
  const find = (lead) => lines.findIndex((l) => new RegExp(`^${lead}(?:const|function) ${name}\\b`).test(l));
  const top = find('(?:export )?');
  const start = top >= 0 ? top : find('\\s+');
  if (start < 0) return null;
  let depth = 0;
  for (let i = start; i < lines.length; i++) {
    const line = lines[i];
    for (let j = 0, quote = ''; j < line.length; j++) {
      const c = line[j];
      if (quote) { if (c === '\\') j++; else if (c === quote) quote = ''; continue; }
      if (c === '"' || c === "'" || c === '`') { quote = c; continue; }
      if (c === '/' && line[j + 1] === '/') break;
      if (c === '/' && line[j + 1] === '*') { const end = line.indexOf('*/', j + 2); j = end < 0 ? line.length : end + 1; continue; }
      if (c === '/' && REGEX_AFTER.test(line.slice(0, j).trimEnd() || '(')) {
        j = regexEnd(line, j);
        continue;
      }
      if ('([{'.includes(c)) depth++;
      else if (')]}'.includes(c)) depth--;
    }
    if (depth <= 0 && /[;}]\s*$/.test(line)) return lines.slice(start, i + 1).join('\n');
  }
  return null;
};

// What a declaration says, apart from where it sits: its indent and an `export`.
export const declarationShape = (decl) => {
  if (!decl) return decl;
  const pad = /^\s*/.exec(decl)[0];
  return decl.split('\n').map((l) => (l.startsWith(pad) ? l.slice(pad.length) : l)).join('\n').replace(/^export /, '');
};
// `want` placed where `mine` sits: mine's indent and mine's `export`.
const placed = (want, mine) => {
  const pad = /^\s*/.exec(mine)[0];
  const lead = mine.trimStart().startsWith('export ') ? 'export ' : '';
  return (lead + declarationShape(want)).split('\n').map((l) => (l ? pad + l : l)).join('\n');
};

// ── rows → work items ───────────────────────────────────────────
const jsFiles = (dir) => readdirSync(dir, { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))
  .flatMap((e) => (e.isDirectory() ? jsFiles(path.join(dir, e.name)) : e.name.endsWith('.js') ? [path.join(dir, e.name)] : []));
const filesOf = (abs) => (abs.endsWith('/') ? jsFiles(abs) : [abs]);
const read = (abs) => readFileSync(abs, 'utf8');
const listTree = (abs, ext) => (existsSync(abs) ? readdirSync(abs).filter((f) => ext.some((e) => f.endsWith(e))).sort() : []);

// Each item: { label, test, from, to, drift(): string[] (empty when in step), sync(): string[] }.
export const itemsOf = (m, root = ROOT) => {
  const at = (rel) => path.join(root, rel);
  const bytesItem = (label, test, from, to, want) => ({
    label, test, from, to,
    drift: () => (!existsSync(at(to)) ? ['missing'] : read(at(to)) === want() ? [] : ['bytes differ']),
    sync: () => { writeFileSync(at(to), want()); return [`wrote ${to}`]; },
  });
  const items = [];
  for (const r of m.copies) items.push(bytesItem(`copy ${r.to}`, r.test, r.from, r.to, () => read(at(r.from))));
  // Written by a build step (`by`) and gitignored: absent is fine, present must match.
  for (const r of m.generated ?? []) {
    const item = bytesItem(`generated ${r.to}`, r.test, r.from, r.to, () => read(at(r.from)));
    items.push({ ...item, drift: () => (existsSync(at(r.to)) ? item.drift() : []) });
  }
  for (const r of m.picks) {
    const want = () => { const c = JSON.parse(read(at(r.from))); return `${JSON.stringify(Object.fromEntries(r.keys.map((k) => [k, c[k]])), null, 2)}\n`; };
    items.push(bytesItem(`pick ${r.to}`, r.test, r.from, r.to, want));
  }
  for (const r of m.trees) {
    const names = () => ({ src: listTree(at(r.from), r.ext), dst: listTree(at(r.to), r.ext) });
    // A tree copy equals its original but for the import specifiers `rewrite` declares.
    const want = (f) => Object.entries(r.rewrite ?? {}).reduce((t, [a, b]) => t.split(a).join(b), read(at(r.from + f)));
    items.push({
      label: `tree ${r.to}`, test: r.test, from: r.from, to: r.to,
      drift: () => {
        const { src, dst } = names();
        return [...src.filter((f) => !dst.includes(f)).map((f) => `missing ${f}`),
          ...dst.filter((f) => !src.includes(f)).map((f) => `orphan ${f}`),
          ...src.filter((f) => dst.includes(f) && want(f) !== read(at(r.to + f))).map((f) => `${f} differs`)];
      },
      sync: () => {
        const { src, dst } = names();
        const done = [];
        for (const f of src) if (want(f) !== (existsSync(at(r.to + f)) ? read(at(r.to + f)) : null)) {
          writeFileSync(at(r.to + f), want(f)); done.push(`wrote ${r.to}${f}`);
        }
        for (const f of dst.filter((x) => !src.includes(x))) { rmSync(at(r.to + f)); done.push(`removed ${r.to}${f}`); }
        return done;
      },
    });
  }
  for (const r of m.ports) {
    items.push({
      label: `port ${r.name}`, test: r.test ?? PORT_TEST, from: r.from, to: r.to,
      drift: () => (normalizePort(read(at(r.from))) === normalizePort(read(at(r.to))) ? [] : ['body differs']),
      sync: () => {
        const { text, missing } = regeneratePort(read(at(r.from)), read(at(r.to)));
        if (missing.length) return [`SKIPPED ${r.to}: no spelling here for ${missing.join(', ')} — edit by hand`];
        writeFileSync(at(r.to), text); return [`wrote ${r.to}`];
      },
    });
  }
  for (const r of m.functions) {
    const theirs = (fn) => { const src = filesOf(at(r.from)).map(read).join('\n');
      return declaration(src, `${fn}JS`)?.replace(`${fn}JS`, fn) ?? declaration(src, fn); };
    const home = (fn) => filesOf(at(r.to)).find((f) => declaration(read(f), fn));
    items.push({
      label: `functions ${r.name}`, test: r.test ?? FUNCTIONS_TEST, from: r.from, to: r.to,
      drift: () => r.names.filter((fn) => {
        const h = home(fn); return !h || declarationShape(declaration(read(h), fn)) !== declarationShape(theirs(fn)); })
        .map((fn) => `${fn} differs`),
      sync: () => r.names.flatMap((fn) => {
        const h = home(fn); const want = theirs(fn);
        if (!h || !want) return [`SKIPPED ${r.name}.${fn}: not found on both sides — edit by hand`];
        const text = read(h); const mine = declaration(text, fn);
        if (declarationShape(mine) === declarationShape(want)) return [];
        writeFileSync(h, text.replace(mine, () => placed(want, mine))); return [`wrote ${path.relative(root, h)} (${fn})`];
      }),
    });
  }
  return items;
};

// ── CLI ─────────────────────────────────────────────────────────
const touches = (item, filters) => !filters.length
  || filters.some((f) => [item.from, item.to].some((p) => p.startsWith(f) || f.startsWith(p)));

// The PostToolUse note for one edited file: null when it is no twin's side or every pair is in step.
export const hookNote = (payload, root = ROOT) => {
  const file = payload?.tool_input?.file_path;
  if (typeof file !== 'string' || !file) return null;
  const at = path.relative(root, path.resolve(root, file));
  if (at.startsWith('..')) return null;
  const drifted = itemsOf(loadManifest(root), root).filter((i) => touches(i, [at]) && i.drift().length);
  if (!drifted.length) return null;
  const copy = drifted.find((i) => at.startsWith(i.to));
  return copy
    ? `${at} is a copy of ${copy.from} — edit the original, then run node tools/syncTwins.mjs`
    : `${at} has twins out of step (${drifted.map((i) => i.to).join(', ')}) — run node tools/syncTwins.mjs`;
};

if (import.meta.url === pathToFileURL(process.argv[1] || '').href && process.argv[2] === '--hook') {
  let input = '';
  process.stdin.on('data', (c) => { input += c; });
  process.stdin.on('end', () => {
    let note = null;
    try { note = hookNote(JSON.parse(input)); } catch { note = null; }
    if (note) process.stderr.write(`${note}\n`);
    process.exit(note ? 2 : 0);
  });
} else if (import.meta.url === pathToFileURL(process.argv[1] || '').href) {
  const args = process.argv.slice(2);
  const check = args.includes('--check');
  const filters = args.filter((a) => a !== '--check').map((a) => path.relative(ROOT, path.resolve(a)));
  let drifted = 0;
  for (const item of itemsOf(loadManifest()).filter((i) => touches(i, filters))) {
    const drift = item.drift();
    if (!drift.length) continue;
    drifted++;
    if (check) console.log(`DRIFT ${item.label}  (from ${item.from}; pinned by ${item.test})\n  ${drift.join('\n  ')}`);
    else for (const line of item.sync()) console.log(line);
  }
  console.log(`syncTwins: ${drifted} pair${drifted === 1 ? '' : 's'} ${check ? 'drifted' : 're-synced'}`);
  process.exit(check && drifted ? 1 : 0);
}

// core/CMakeLists.txt is the one core source and include-dir list; cli/build.zig and
// pystencil/build.py recompile the same files and must name the same entries (order is free).
//   node tools/repo/coreLists.mjs --check   (report drift, exit 1)
//   node tools/repo/coreLists.mjs --write   (rewrite the zig and py lists from CMake)
import { readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');

// The extern "C" ABI each recompiling surface appends: cliApi.cpp, never a wasm*Api.cpp.
export const ABI_SOURCE = 'cliApi.cpp';

const block = (text, start, end) => {
  const at = text.indexOf(start);
  if (at < 0) return null;
  const from = at + start.length;
  const to = text.indexOf(end, from);
  return to < 0 ? null : { from, to, body: text.slice(from, to) };
};

const quoted = (body) => [...body.matchAll(/"([^"]+)"/g)].map((m) => m[1]);

export const cmakeLists = (text) => {
  const src = block(text, 'set(STENCIL_CORE_SOURCES', ')');
  const inc = block(text, 'set(STENCIL_CORE_INCLUDE_DIRS', ')');
  if (!src || !inc) return null;
  const words = (body) => body.replace(/#.*$/gm, '').split(/\s+/).filter(Boolean);
  const dirs = words(inc.body).map((w) => w.replace(/^\$\{CMAKE_CURRENT_SOURCE_DIR\}\/?/, '') || '.');
  return { sources: words(src.body), includeDirs: dirs };
};

const ZIG_SOURCES = ['const core_sources = [_][]const u8{', '};'];
const ZIG_DIRS = ['const core_include_dirs = [_][]const u8{', '};'];
const PY_SOURCES = ['STENCIL_CORE_SOURCES = [', ']'];
const PY_DIRS = ['INCLUDE_DIRS = [', ']'];

export const zigLists = (text) => {
  const src = block(text, ...ZIG_SOURCES);
  const inc = block(text, ...ZIG_DIRS);
  if (!src || !inc) return null;
  return {
    sources: quoted(src.body).filter((f) => f !== ABI_SOURCE),
    includeDirs: quoted(inc.body).map((d) => d.replace(/^\.\.\/core\/?/, '') || '.'),
  };
};

export const pyLists = (text) => {
  const src = block(text, ...PY_SOURCES);
  const inc = block(text, ...PY_DIRS);
  if (!src || !inc) return null;
  return { sources: quoted(src.body), includeDirs: quoted(inc.body) };
};


// One line per list that differs from CMake's, naming what it lacks and what it adds.
export const drift = (name, want, got) => {
  if (!got) return [`${name}: the core lists were not found`];
  const out = [];
  for (const key of ['sources', 'includeDirs']) {
    const lacks = want[key].filter((v) => !got[key].includes(v));
    const adds = got[key].filter((v) => !want[key].includes(v));
    const twice = got[key].filter((v, i) => got[key].indexOf(v) !== i);
    const what = [lacks.length && `lacks ${lacks.join(', ')}`, adds.length && `adds ${adds.join(', ')}`,
      twice.length && `repeats ${twice.join(', ')}`].filter(Boolean).join('; ');
    if (what) out.push(`${name} ${key}: ${what}`);
  }
  return out;
};

const splice = (text, [start, end], body) => {
  const b = block(text, start, end);
  return text.slice(0, b.from) + body + text.slice(b.to);
};

export const writeZig = (text, want) => {
  const lines = (items) => `\n${items.map((v) => `    "${v}",`).join('\n')}\n`;
  const dirs = want.includeDirs.map((d) => (d === '.' ? '../core' : `../core/${d}`));
  return splice(splice(text, ZIG_SOURCES, lines([...want.sources, ABI_SOURCE])), ZIG_DIRS, lines(dirs));
};

export const writePy = (text, want) => {
  const sources = `\n${want.sources.map((v) => `  "${v}",`).join('\n')}\n`;
  const rows = [];
  let row = '';
  for (const d of want.includeDirs.map((v) => `"${v}"`)) {
    if (row && `${row}, ${d}`.length > 72) { rows.push(`${row},`); row = d; } else row = row ? `${row}, ${d}` : d;
  }
  rows.push(row);
  return splice(splice(text, PY_SOURCES, sources), PY_DIRS, rows.join(`\n${' '.repeat(PY_DIRS[0].length)}`));
};

const FILES = { cmake: 'core/CMakeLists.txt', zig: 'cli/build.zig', py: 'pystencil/build.py' };

export const run = (mode, root = ROOT) => {
  const read = (rel) => readFileSync(path.join(root, rel), 'utf8');
  const want = cmakeLists(read(FILES.cmake));
  if (!want) return { problems: [`${FILES.cmake}: STENCIL_CORE_SOURCES or STENCIL_CORE_INCLUDE_DIRS not found`] };
  const zig = read(FILES.zig);
  const py = read(FILES.py);
  const problems = [...drift(FILES.zig, want, zigLists(zig)), ...drift(FILES.py, want, pyLists(py))];
  if (mode === '--write' && problems.length) {
    writeFileSync(path.join(root, FILES.zig), writeZig(zig, want));
    writeFileSync(path.join(root, FILES.py), writePy(py, want));
    return { problems: [], wrote: problems };
  }
  return { problems };
};

if (import.meta.url === pathToFileURL(process.argv[1] || '').href) {
  const mode = process.argv[2] ?? '--check';
  if (!['--check', '--write'].includes(mode)) {
    console.error('usage: node tools/repo/coreLists.mjs [--check|--write]');
    process.exit(2);
  }
  const { problems, wrote = [] } = run(mode);
  for (const p of [...problems, ...wrote]) console.log(`  ${p}`);
  console.log(`coreLists: ${problems.length} drifted${wrote.length ? `, ${wrote.length} rewritten` : ''}`);
  process.exit(problems.length ? 1 : 0);
}

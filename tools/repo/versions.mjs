// An app's version is the one its manifest declares; Stencil's is the newest heading of the root
// CHANGELOG.md. Lockfiles and README badges follow them; changelog entries are checked, never written.
//   node tools/repo/versions.mjs --check   (report drift, exit 1)
//   node tools/repo/versions.mjs --write   (rewrite the lockfiles and badges that drifted)
//   node tools/repo/versions.mjs --get <app|stencil>   (print one version, for a release job)
import { readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
export const CHANGELOG = 'CHANGELOG.md';

const V = String.raw`(\d+\.\d+\.\d+)`;
const RELEASE = `^## \\[${V}\\]`;
const pkg = (dir) => [`${dir}/package.json`, `^  "version": "${V}"`];
const lock = (dir) => [
  [`${dir}/package-lock.json`, `^  "version": "${V}"`],
  [`${dir}/package-lock.json`, `^    "": \\{\\n      "name": "[^"]+",\\n      "version": "${V}"`],
];

// Per app (keyed by its folder): the badge label, its name in the root changelog, the manifest
// that declares its version, and the files that repeat it.
export const APPS = {
  core: { label: 'core', title: 'Core', source: ['core/CMakeLists.txt', `^project\\(stencil_core VERSION ${V}`] },
  browser: { label: 'browser', title: 'Browser', source: pkg('browser'), copies: lock('browser') },
  desktop: { label: 'desktop', title: 'Desktop', source: ['desktop/CMakeLists.txt', `^project\\(stencil VERSION ${V}`] },
  cli: { label: 'cli', title: 'CLI', source: ['cli/build.zig.zon', `^ *\\.version = "${V}"`] },
  pystencil: {
    label: 'pystencil',
    title: 'pystencil',
    source: ['pystencil/pyproject.toml', `^version = "${V}"`],
    copies: [['pystencil/pystencil/__init__.py', `^__version__ = "${V}"`]],
  },
  'browser-extension': {
    label: 'chrome_extension',
    title: 'Chrome extension',
    source: ['browser-extension/manifest.json', `^  "version": "${V}"`],
    copies: [pkg('browser-extension')],
  },
  'vscode-extension': { label: 'vscode_extension', title: 'VS Code extension', source: pkg('vscode-extension'), copies: lock('vscode-extension') },
  mcp: {
    label: 'mcp',
    title: 'MCP server',
    source: ['mcp/Cargo.toml', `^version = "${V}"`],
    copies: [['mcp/Cargo.lock', `^name = "stencil-mcp"\\nversion = "${V}"`]],
  },
  server: { label: 'server', title: 'Collaboration server', source: ['server/cmd/stencil-server/main.go', `^const version = "${V}"`] },
  bot: { label: 'bot', title: 'Telegram bot', source: ['bot/src/Stencil.TelegramBot.Bot/Stencil.TelegramBot.Bot.csproj', `<Version>${V}</Version>`] },
};

const badge = (label) => `img\\.shields\\.io/badge/${label}-${V}-`;
const site = (file, re, what, byHand = false) => ({ file, re, what, byHand });

// One group per version: where it is declared, and every place that must repeat it. A `byHand`
// follower is a changelog entry: reported when it lags, never rewritten.
export const groups = () => [
  { source: site(CHANGELOG, RELEASE, 'newest release heading'), followers: [site('README.md', badge('stencil'), 'stencil badge')] },
  ...Object.entries(APPS).map(([app, { label, title, source, copies = [] }]) => ({
    source: site(...source, `${app} version`),
    followers: [
      ...copies.map(([file, re]) => site(file, re, `${app} version`)),
      site('README.md', badge(label), `${app} badge`),
      site(`${app}/README.md`, badge(label), `${app} badge`),
      site(`${app}/${CHANGELOG}`, RELEASE, 'newest release heading', true),
      site(CHANGELOG, `^\\| \\[${title}\\]\\(${app}/${CHANGELOG}\\) \\|[^|\\n]*\\| ${V} \\|`, `${app} row`, true),
    ],
  })),
];

export const files = () => [...new Set(groups().flatMap((g) => [g.source, ...g.followers]).map((s) => s.file))];

export const run = ({ root = ROOT, write = false } = {}) => {
  const texts = new Map();
  const read = (file) => {
    if (!texts.has(file)) {
      let text = null;
      try { text = readFileSync(path.join(root, file), 'utf8'); } catch { /* reported per site */ }
      texts.set(file, text);
    }
    return texts.get(file);
  };
  const find = ({ file, re }) => (read(file) === null ? null : new RegExp(re, 'md').exec(read(file)));
  const problems = [];
  const written = new Set();
  for (const { source, followers } of groups()) {
    const want = find(source)?.[1];
    if (!want) { problems.push(`${source.file}: no ${source.what}`); continue; }
    for (const f of followers) {
      const m = find(f);
      if (!m) { problems.push(`${f.file}: no ${f.what}`); continue; }
      if (m[1] === want) continue;
      if (!write || f.byHand) { problems.push(`${f.file}: ${f.what} is ${m[1]}, ${source.file} says ${want}`); continue; }
      const [from, to] = m.indices[1];
      texts.set(f.file, read(f.file).slice(0, from) + want + read(f.file).slice(to));
      written.add(f.file);
    }
  }
  for (const file of written) writeFileSync(path.join(root, file), texts.get(file));
  return { problems, written: [...written] };
};

// The version `name` declares: an app folder from APPS, or 'stencil'; null when it has none.
export const versionOf = (name, root = ROOT) => {
  const source = name === 'stencil' ? groups()[0].source : APPS[name] && groups()[Object.keys(APPS).indexOf(name) + 1].source;
  if (!source) return null;
  let text;
  try { text = readFileSync(path.join(root, source.file), 'utf8'); } catch { return null; }
  return new RegExp(source.re, 'm').exec(text)?.[1] ?? null;
};

const main = () => {
  const args = process.argv.slice(2);
  if (args[0] === '--get' && args.length === 2) {
    const v = versionOf(args[1]);
    if (!v) { console.error(`versions: no version for ${args[1]}`); return 1; }
    console.log(v);
    return 0;
  }
  if (args.length !== 1 || !['--check', '--write'].includes(args[0])) {
    console.error('usage: node tools/repo/versions.mjs --check | --write | --get <app|stencil>');
    return 2;
  }
  const { problems, written } = run({ write: args[0] === '--write' });
  for (const p of problems) console.log(p);
  for (const f of written) console.log(`rewrote ${f}`);
  console.log(`versions: ${problems.length} out of step${written.length ? `, ${written.length} rewritten` : ''}`);
  return problems.length ? 1 : 0;
};

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) process.exit(main());

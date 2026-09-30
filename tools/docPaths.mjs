// ── docPaths: does every path a doc names still exist? ────
// Reads the backticked paths and relative links of every tracked (or would-be-tracked) .md and
// resolves each against the repo root, the doc's folder, its surface root and that root's js/ + src/.
//   node tools/docPaths.mjs [--check] [doc.md...]   (--check exits 1 on a dead path)
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import path from 'node:path';

export const EXT = /\.(js|mjs|cjs|ts|json|md|cpp|cc|hpp|h|c|mm|zig|py|rs|go|cs|csproj|slnx|txt|qss|css|html|svg|png|gif|yml|yaml|toml|stc|stcjs|pystc|cmake|qrc|sh|ps1|lock|mod|sum|ico|vsix|plist|nsi|rc)$/i;
const MIME = /^(application|image|text|video|audio|font|multipart|model)\/[\w.+-]+$/;
const DOMAIN = /^[\w-]+(\.[\w-]+)*\.(org|com|net|io|dev|ai|app|sh)$/;
const PATH_CHARS = /^[\w.\-/*{},<>@+…]+$/;
// Build output: named in docs, never in git (anything else gitignored is asked of git itself).
export const BUILD_OUTPUT = /(^|\/)(zig-out|\.zig-cache|target|build|node_modules|\.out|obj|test-state|third_party)(\/|$)|(^|\/)js\/wasm\/|^bot\/packages\//;

// Words that read as paths but name something else: a placeholder, a package's contents, a URL route.
export const ALLOW = Object.freeze({
  '.claude/rules/architecture.md': ['helpers/', 'parts/', 'misc/', 'ui/openImage/openImageTabs.js'],
  'cli/ARCHITECTURE.md': ['x/'],
  'cli/CONTRACT.md': ['shots/a.png', 'shots/a-stencil.png'],
  'desktop/README.md': ['bin/'],
  'e2e/ARCHITECTURE.md': ['__e2e__/page-with-image.html'],
});

// ── extraction ──────────────────────────────────────────────────
export const extractRefs = (md) => {
  const refs = [];
  let fence = null;
  md.split('\n').forEach((text, i) => {
    const open = /^\s*(`{3,}|~{3,})/.exec(text);
    if (open) {
      if (!fence) fence = open[1][0];
      else if (open[1][0] === fence) fence = null;
      return;
    }
    if (fence) return;
    const line = i + 1;
    for (const m of text.matchAll(/(?<!`)`([^`\n]+)`(?!`)/g))
      for (const word of m[1].trim().split(/\s+/)) refs.push({ token: word, line, kind: 'code' });
    for (const m of text.replace(/`[^`\n]*`/g, '').matchAll(/\]\(([^)\s]+)(?:\s+"[^"]*")?\)/g))
      refs.push({ token: m[1], line, kind: 'link' });
  });
  return refs;
};

// ── the file universe ───────────────────────────────────────────
const tailsOf = (segs, into) => {
  for (let i = 0; i < segs.length; i++) {
    into.add(segs.slice(i).join('/'));
    // `Application/…` abbreviates `Stencil.TelegramBot.Application/…`.
    if (i < segs.length - 1 && /^[A-Z]\w*(\.[A-Z]\w*)+$/.test(segs[i]))
      into.add([segs[i].split('.').pop(), ...segs.slice(i + 1)].join('/'));
  }
};

export const makeUniverse = (files) => {
  const fileSet = new Set(files);
  const dirSet = new Set();
  const tails = new Set();             // every trailing run of segments, of files and dirs
  const stems = new Set();             // the same, with the basename cut at one of its dots
  for (const f of files) {
    const segs = f.split('/');
    for (let i = 1; i < segs.length; i++) dirSet.add(segs.slice(0, i).join('/'));
    tailsOf(segs, tails);
    const base = segs[segs.length - 1];
    // A test's name before its role (`llmClient` of `llmClient.test.js`) is no module of its own.
    const role = /\.(test|spec|headless|gui)\./.exec(base);
    for (let d = base.indexOf('.', role ? role.index + 1 : 0); d > 0; d = base.indexOf('.', d + 1))
      tailsOf([...segs.slice(0, -1), base.slice(0, d)], stems);
  }
  for (const d of dirSet) tailsOf(d.split('/'), tails);
  return { fileSet, dirSet, tails, tailList: [...tails], stems };
};

const basesOf = (doc) => {
  const dir = path.posix.dirname(doc);
  const top = doc.includes('/') ? doc.split('/')[0] : '';
  const bases = ['', dir === '.' ? '' : dir];
  if (top && !top.startsWith('.')) bases.push(top, `${top}/js`, `${top}/src`);
  return [...new Set(bases)];
};

const join = (base, token) => {
  const p = path.posix.normalize(base ? `${base}/${token}` : token);
  return p.startsWith('..') ? null : (p === '.' ? '' : p.replace(/\/$/, ''));
};

// A code word is a path when it is path-shaped and its head is something the tree could hold.
export const isCandidate = (word, doc, u) => {
  if (!word.includes('/') || word.includes('://') || MIME.test(word)) return false;
  if (/^[/~$-]/.test(word) || !PATH_CHARS.test(word)) return false;
  const head = word.split('/')[0];
  if (DOMAIN.test(head) || /^[{<].*[}>]$/.test(head)) return false;
  const clean = word.replace(/[.,;:]+$/, '');
  if (EXT.test(clean.replace(/[{}*<>]/g, '')) || clean.endsWith('/')) return true;
  if (head === '.' || head === '..' || head === '...' || head === '…') return true;
  return u.tails.has(head) && !u.fileSet.has(head);
};

const expandBraces = (s) => {
  const m = /\{([^{}]*)\}/.exec(s);
  if (!m || !m[1].includes(',')) return [s];
  return m[1].split(',').flatMap((alt) => expandBraces(s.slice(0, m.index) + alt + s.slice(m.index + m[0].length)));
};

// `*` stays in one segment, `**` spans any depth; a trailing `.ext` run is allowed after the last.
const globRe = (g) => {
  const segs = g.split('/');
  const body = segs.map((seg, i) => (seg === '**' ? '(?:[^/]+/)*'
    : seg.replace(/[.+^${}()|[\]\\]/g, '\\$&').replace(/\*/g, '[^/]*') + (i < segs.length - 1 ? '/' : '')));
  return new RegExp(`(^|/)${body.join('')}(\\.[^/]+)*$`);
};

// One alternative resolves as a file, a directory or a module stem, under any base or as a tail.
const resolvesOne = (alt, doc, u) => {
  const bare = alt.replace(/\/$/, '').replace(/^\.\//, '');
  if (bare === '' || bare === '.') return true;
  if (bare.includes('*')) {
    const re = globRe(bare);
    return u.tailList.some((t) => re.test(t));
  }
  for (const b of basesOf(doc)) {
    const p = join(b, bare);
    if (p === '' || (p && (u.fileSet.has(p) || u.dirSet.has(p) || u.stems.has(p)))) return true;
  }
  if (u.tails.has(bare) || u.stems.has(bare)) return true;
  // `module.member`: a stem followed by an identifier that is no file extension.
  const member = /^(.*\/[^/.]+)\.[A-Za-z_]\w*$/.exec(bare);
  return Boolean(member && !EXT.test(bare) && resolvesOne(member[1], doc, u));
};

const cleanOf = (token) => token.replace(/[.,;:]+$/, '').replace(/[#?].*$/, '')
  .replace(/\/(\.\.\.|…|\*\*)$/, '').replace(/(^|\/)(\.\.\.|…)(?=\/)/g, '$1**').replace(/<[^>/]*>/g, '*');

export const resolves = (token, doc, u) => expandBraces(cleanOf(token)).every((alt) => resolvesOne(alt, doc, u));

const linkResolves = (token, doc, u) => {
  let target;
  try { target = decodeURI(token.replace(/[#?].*$/, '')); } catch { return false; }
  const p = join(path.posix.dirname(doc) === '.' ? '' : path.posix.dirname(doc), target);
  return p !== null && (p === '' || u.fileSet.has(p) || u.dirSet.has(p));
};

// ── the scan ────────────────────────────────────────────────────
const git = (root, ...args) => execFileSync('git', ['-C', root, ...args], { encoding: 'utf8', maxBuffer: 64 << 20 });

const ignoredByGit = (root, paths) => {
  if (!paths.length) return new Set();
  try {
    return new Set(execFileSync('git', ['-C', root, 'check-ignore', '--no-index', '--stdin'],
      { input: paths.join('\n'), encoding: 'utf8' }).split('\n').filter(Boolean));
  } catch (err) {
    return new Set(String(err.stdout || '').split('\n').filter(Boolean)); // exit 1: none ignored
  }
};

export const deadRefs = (doc, md, u) => extractRefs(md).filter(({ token, kind }) => {
  if ((ALLOW[doc] || []).includes(token)) return false;
  if (kind === 'link') return !/^([a-z][\w+.-]*:|#)/i.test(token) && !linkResolves(token, doc, u);
  return isCandidate(token, doc, u) && !resolves(token, doc, u) && !BUILD_OUTPUT.test(token);
});

export const scan = (root, only = []) => {
  const files = git(root, 'ls-files', '-co', '--exclude-standard').split('\n')
    .filter((f) => f && existsSync(path.join(root, f)));
  const u = makeUniverse(files);
  const docs = only.length ? only : files.filter((f) => f.endsWith('.md'));
  return dropIgnored(root, docs.flatMap((doc) => deadRefs(doc, readFileSync(path.join(root, doc), 'utf8'), u)
    .map((ref) => ({ doc, ...ref }))));
};

// A dead path that names something git ignores (a local secret, a recording) is no dead path.
// Each is asked with a trailing slash too: a `dir/` pattern never matches a directory not on disk.
export const dropIgnored = (root, dead) => {
  const under = (d) => basesOf(d.doc).map((b) => join(b, cleanOf(d.token))).filter(Boolean)
    .flatMap((p) => [p, `${p}/`]);
  const ignored = ignoredByGit(root, [...new Set(dead.flatMap(under))]);
  return dead.filter((d) => !under(d).some((p) => ignored.has(p)));
};

if (import.meta.url === pathToFileURL(process.argv[1] || '').href) {
  const args = process.argv.slice(2);
  const root = git(process.cwd(), 'rev-parse', '--show-toplevel').trim();
  const only = args.filter((a) => a !== '--check').map((a) => path.relative(root, path.resolve(a)));
  const dead = scan(root, only);
  for (const d of dead) console.log(`${d.doc}:${d.line}  ${d.kind === 'link' ? 'link' : 'path'}  ${d.token}`);
  console.log(`docPaths: ${dead.length} dead path${dead.length === 1 ? '' : 's'}`);
  process.exit(args.includes('--check') && dead.length ? 1 : 0);
}

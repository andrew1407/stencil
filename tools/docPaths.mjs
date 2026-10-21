// ── docPaths: does every path a doc names still exist? ────
// Reads the backticked paths, relative links and HTML src/href of every tracked (or would-be-tracked)
// .md, resolves each against the repo root, the doc's folder, its surface root and that root's
// js/ + src/, and holds every #fragment into a .md to that doc's heading slugs and ids.
//   node tools/docPaths.mjs [--check] [doc.md or folder...]   (--check exits 1 on a dead path)
import { execFile } from 'node:child_process';
import { pathToFileURL } from 'node:url';
import path from 'node:path';
import { listFiles, mapLimit, readText, repoRoot, under } from './repo/io.mjs';

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
    const prose = text.replace(/`[^`\n]*`/g, '');
    for (const m of prose.matchAll(/\]\(([^)\s]+)(?:\s+"[^"]*")?\)|<[a-z]+\s[^>]*?\b(?:src|href)="([^"]+)"/gi))
      refs.push({ token: m[1] || m[2], line, kind: 'link' });
  });
  return refs;
};

// GitHub's heading slug: lowercase, punctuation and symbols dropped, each space a hyphen.
export const slugOf = (heading) => heading.trim().toLowerCase().replace(/!?\[([^\]]*)\]\([^)]*\)/g, '$1')
  .replace(/<[^>]+>/g, '').replace(/[^\p{L}\p{N}\p{M}\s_-]/gu, '').replace(/\s/g, '-');

// Every anchor a doc offers: its headings' slugs (a repeat gets -1, -2, …) and explicit id= / name=.
export const anchorsOf = (md) => {
  const out = new Set();
  let fence = null;
  for (const text of md.split('\n')) {
    const open = /^\s*(`{3,}|~{3,})/.exec(text);
    if (open) { fence = !fence ? open[1][0] : (open[1][0] === fence ? null : fence); continue; }
    if (fence) continue;
    const h = /^#{1,6}\s+(.*?)\s*#*\s*$/.exec(text);
    if (h) {
      let slug = slugOf(h[1]);
      for (let n = 1, base = slug; out.has(slug); n++) slug = `${base}-${n}`;
      out.add(slug);
    }
    for (const m of text.matchAll(/\b(?:id|name)="([^"]+)"/g)) out.add(m[1]);
  }
  return out;
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
// Exit 1 means none ignored, so stdout is read whatever the exit code.
const ignoredByGit = (root, paths) => (!paths.length ? Promise.resolve(new Set()) : new Promise((resolve, reject) => {
  const child = execFile('git', ['-C', root, 'check-ignore', '--no-index', '--stdin'], { encoding: 'utf8', maxBuffer: 64 << 20 },
    (err, stdout) => (err && typeof err.code !== 'number' ? reject(err) : resolve(new Set(stdout.split('\n').filter(Boolean)))));
  child.stdin.end(paths.join('\n'));
}));

// A `#fragment` into a Markdown doc must name one of its anchors (`anchors`: doc → anchorsOf).
const anchorDead = (token, doc, anchors) => {
  const hash = token.indexOf('#');
  if (!anchors || hash < 0) return false;
  const file = token.slice(0, hash).replace(/\?.*$/, '');
  const target = file ? join(path.posix.dirname(doc) === '.' ? '' : path.posix.dirname(doc), decodeURI(file)) : doc;
  const offered = target && target.endsWith('.md') ? anchors.get(target) : null;
  return Boolean(offered) && !offered.has(decodeURIComponent(token.slice(hash + 1)));
};

export const deadRefs = (doc, md, u, anchors = null) => extractRefs(md).filter(({ token, kind }) => {
  if ((ALLOW[doc] || []).includes(token)) return false;
  if (kind === 'link') {
    if (/^[a-z][\w+.-]*:/i.test(token)) return false;
    return (!token.startsWith('#') && !linkResolves(token, doc, u)) || anchorDead(token, doc, anchors);
  }
  return isCandidate(token, doc, u) && !resolves(token, doc, u) && !BUILD_OUTPUT.test(token);
});

const listUnder = (files, dir) => files.filter((f) => f.endsWith('.md') && under(f, [dir]));

// `only` names docs, or folders whose docs are read.
export const scan = async (root, only = []) => {
  const files = await listFiles(root);
  const u = makeUniverse(files);
  const docs = only.length ? only.flatMap((o) => (u.dirSet.has(o.replace(/\/$/, '')) ? listUnder(files, o) : [o]))
    : files.filter((f) => f.endsWith('.md'));
  const allMd = files.filter((f) => f.endsWith('.md'));
  const texts = new Map(await mapLimit(allMd, async (doc) => [doc, await readText(root, doc)]));
  const anchors = new Map([...texts].map(([doc, md]) => [doc, anchorsOf(md)]));
  const dead = await mapLimit(docs, async (doc) => deadRefs(doc, texts.get(doc) ?? await readText(root, doc), u, anchors)
    .map((ref) => ({ doc, ...ref })));
  return dropIgnored(root, dead.flat());
};

// A dead path that names something git ignores (a local secret, a recording) is no dead path.
// Each is asked with a trailing slash too: a `dir/` pattern never matches a directory not on disk.
export const dropIgnored = async (root, dead) => {
  const under = (d) => basesOf(d.doc).map((b) => join(b, cleanOf(d.token))).filter(Boolean)
    .flatMap((p) => [p, `${p}/`]);
  const ignored = await ignoredByGit(root, [...new Set(dead.flatMap(under))]);
  return dead.filter((d) => !under(d).some((p) => ignored.has(p)));
};

if (import.meta.url === pathToFileURL(process.argv[1] || '').href) {
  const args = process.argv.slice(2);
  const root = await repoRoot();
  const only = args.filter((a) => a !== '--check').map((a) => path.relative(root, path.resolve(a)));
  const dead = await scan(root, only);
  for (const d of dead) console.log(`${d.doc}:${d.line}  ${d.kind === 'link' ? 'link' : 'path'}  ${d.token}`);
  console.log(`docPaths: ${dead.length} dead path${dead.length === 1 ? '' : 's'}`);
  process.exit(args.includes('--check') && dead.length ? 1 : 0);
}

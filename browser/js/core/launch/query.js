// The launch query: `?key=value` pairs on the page URL, or the same pairs in the fragment, read
// once at boot and applied through the facade (console/launchQuery.js). The `stencil://` keys of
// buildStencilSchemeUrl are taken as they are, so a desktop link's query pastes into a browser
// URL. Pure string work; the corpus is common/fixtures/deepLink/launchQuery.json.
import constants from '../../../../common/config/constants.json' with { type: 'json' };
import GRAMMAR from '../../../../common/config/launchQuery.json' with { type: 'json' };

const SCRIPT_MAX = constants.LAUNCH.scriptMaxChars;

// Canonical key ← every spelling of it; a key in neither column is a stencil.settings candidate.
const CANONICAL = new Map(Object.entries(GRAMMAR.keys).flatMap(([k, list]) => list.map((s) => [s, k])));

// A credential rides the fragment only: the query lands in server logs and history.
export const LAUNCH_QUERY_SECRET_KEYS = Object.freeze([...GRAMMAR.secretKeys]);

const THEME_WORDS = GRAMMAR.themeWords;
const CHAT_DOCKS = GRAMMAR.chatDocks;
const TRUE_WORDS = GRAMMAR.trueWords;
const FALSE_WORDS = GRAMMAR.falseWords;

// `mainColor`, `main_color` and `main-color` are one key.
const kebab = (k) => String(k).trim().replace(/_/g, '-').replace(/([a-z0-9])([A-Z])/g, '$1-$2')
  .toLowerCase().replace(/^-+|-+$/g, '');
const camel = (k) => k.replace(/-([a-z0-9])/g, (_, c) => c.toUpperCase());
const UNSAFE_KEYS = ['__proto__', 'constructor', 'prototype'];

// A bare flag (`?incognito`), a 1 or a true word is on.
const flag = (v) => { const s = String(v).trim().toLowerCase(); return s === '' || s === '1' || TRUE_WORDS.includes(s); };

// Booleans by word, numbers by shape, the rest as typed: what a setter would get from the console.
export const coerceLaunchValue = (v) => {
  const s = String(v).trim();
  const low = s.toLowerCase();
  if (TRUE_WORDS.includes(low)) return true;
  if (FALSE_WORDS.includes(low)) return false;
  return s !== '' && /^-?\d+(\.\d+)?$/.test(s) ? Number(s) : v;
};

const text = (v) => { const s = String(v ?? '').trim(); return s ? s : null; };
const numberOr = (v, fallback) => { const n = Number(v); return v != null && v !== '' && Number.isFinite(n) ? n : fallback; };
const sizeOr = (v, fallback) => { const n = numberOr(v, NaN); return n > 0 ? n : fallback; };
const isWebUrl = (s) => /^https?:\/\//i.test(s) || /^data:/i.test(s);

// `host[:port]/projects/<id|name>` names the server too; a bare `<id|name>` is the launch's
// server's project when one is named, else a local one. `server=` supplies a missing server part.
const projectLocator = (value, serverKey) => {
  const at = value.indexOf('/projects/');
  const server = at < 0 ? null : value.slice(0, at).replace(/\/+$/, '');
  const ref = (at < 0 ? value : value.slice(at + '/projects/'.length)).replace(/\/+$/, '');
  return ref ? { kind: 'project', server: server || serverKey || null, ref } : null;
};

// `#stencil=` and `#stencil-desktop=` are the JSON hand-offs; any other `#a=b` is this grammar.
export const isLaunchQueryHash = (hash) => {
  const h = String(hash ?? '');
  return h.startsWith('#') && h.includes('=') && !/^#stencil(-desktop)?=/.test(h);
};

const pairsOf = (raw, fromHash) => {
  let params;
  try { params = new URLSearchParams(raw); } catch { return []; }
  return [...params].map(([k, v]) => ({ key: kebab(k), value: v, fromHash }));
};

const parseJsonObject = (s) => {
  try {
    const v = JSON.parse(s);
    return v && typeof v === 'object' && !Array.isArray(v) ? v : null;
  } catch { return null; }
};

// The query that names nothing; a corpus vector's `expect` is merged over it.
export const emptyLaunchQuery = () => ({
  picture: null, name: null, layout: null, incognito: false, script: '',
  theme: null, settings: {}, llm: {}, chat: { open: false, dock: null },
  connect: [], project: { description: null, keywords: null, color: null, expire: null },
  warnings: [], fromHash: false, opensSession: false,
});

// Null when the URL carries nothing of this grammar (a bare `?open=<id>` included).
export const parseLaunchQuery = (search = '', hash = '') => {
  const fromHash = isLaunchQueryHash(hash);
  const pairs = [
    ...pairsOf(String(search ?? '').replace(/^\?/, ''), false),
    ...(fromHash ? pairsOf(String(hash).slice(1), true) : []),
  ];
  if (!pairs.some((p) => p.key !== 'open')) return null;

  const q = { ...emptyLaunchQuery(), fromHash };
  const warn = (code, key) => { q.warnings.push({ code, key }); };
  const byKey = new Map();
  for (const p of pairs) {
    const canonical = CANONICAL.get(p.key);
    if (!canonical) {
      const k = camel(p.key);
      if (k && !UNSAFE_KEYS.includes(k) && !Object.hasOwn(q.settings, k)) q.settings[k] = coerceLaunchValue(p.value);
      continue;
    }
    if (LAUNCH_QUERY_SECRET_KEYS.includes(canonical) && !p.fromHash) { warn('secret-in-query', canonical); continue; }
    if (!byKey.has(canonical)) byKey.set(canonical, []);
    byKey.get(canonical).push(p.value);
  }
  const first = (k) => (byKey.has(k) ? byKey.get(k)[0] : undefined);
  const all = (k) => byKey.get(k) ?? [];

  const server = text(first('server')), id = text(first('id')), source = text(first('source'));
  if (server && id) {
    q.picture = { kind: 'server', url: server, id, version: numberOr(first('version'), 0) };
  } else if (text(first('project'))) {
    q.picture = projectLocator(text(first('project')), server);
    if (!q.picture) warn('bad-value', 'project');
  } else if (source) {
    if (isWebUrl(source)) q.picture = { kind: 'source', url: source, frame: numberOr(first('frame'), null) };
    else warn('bad-value', 'source');
  } else if (byKey.has('blank')) {
    q.picture = { kind: 'blank', color: text(first('blank')) || '#ffffff',
      width: sizeOr(first('width'), null), height: sizeOr(first('height'), null) };
  }
  q.name = text(first('name'));

  const raw = text(first('layout-raw')), layout = text(first('layout'));
  if (raw) {
    const data = parseJsonObject(raw);
    if (data) q.layout = { data }; else warn('bad-json', 'layout-raw');
  } else if (layout) {
    if (layout.startsWith('{')) {
      const data = parseJsonObject(layout);
      if (data) q.layout = { data }; else warn('bad-json', 'layout');
    } else if (/^https?:\/\//i.test(layout)) q.layout = { url: layout };
    else warn('bad-value', 'layout');
  }

  q.incognito = byKey.has('incognito') && flag(first('incognito'));
  const script = String(first('script') ?? '');
  if (script.length > SCRIPT_MAX) warn('bad-value', 'script');
  else q.script = script;

  if (byKey.has('theme')) {
    const mode = THEME_WORDS[String(first('theme')).trim().toLowerCase()];
    if (mode) q.theme = mode; else warn('bad-value', 'theme');
  }
  if (text(first('main-color'))) q.settings.mainTheme = text(first('main-color'));

  for (const [key, field] of [['description', 'description'], ['keywords', 'keywords'],
    ['project-color', 'color'], ['expire', 'expire']]) {
    if (text(first(key))) q.project[field] = text(first(key));
  }

  for (const [key, field] of [['llm-provider', 'provider'], ['llm-model', 'model'], ['llm-base-url', 'baseUrl'],
    ['llm-server-url', 'serverUrl'], ['llm-api-key', 'apiKey']]) {
    if (text(first(key))) q.llm[field] = text(first(key));
  }

  if (byKey.has('chat')) q.chat.open = flag(first('chat')) || String(first('chat')).trim().toLowerCase() === 'open';
  if (byKey.has('chat-dock')) {
    const dock = String(first('chat-dock')).trim().toLowerCase();
    if (CHAT_DOCKS.includes(dock)) q.chat.dock = dock; else warn('bad-value', 'chat-dock');
  }

  const tokens = all('token');
  q.connect = all('connect').map((url, i) => ({ url: text(url), token: String(tokens[i] ?? '') }))
    .filter((c) => c.url);

  q.opensSession = !!q.picture || q.incognito || !!q.script;
  return q;
};

// What a surface can act on: a picture URL carrying grammar-looking parameters is not a link.
export const isActionableLaunch = (q) => !!q && (q.opensSession || q.connect.length > 0 || !!q.layout
  || !!q.theme || Object.keys(q.llm).length > 0 || q.chat.open || !!q.chat.dock);

// A whole URL typed somewhere (`/open <link>`): `http(s)://…?query#fragment` or `stencil://open?…`.
export const parseLaunchUrl = (url) => {
  let u;
  try { u = new URL(String(url ?? '').trim()); } catch { return null; }
  if (!['http:', 'https:', 'stencil:'].includes(u.protocol)) return null;
  const q = parseLaunchQuery(u.search, u.hash);
  return isActionableLaunch(q) ? q : null;
};

// Bearer-authed JSON over server/internal/protocol; a stale session token is re-minted once.
import { normalizeUrl, parseInviteUrl } from './model.js';
import { readBlobCapped } from './urlGuard.js';
import { MAX_ERROR_BYTES, readJsonCapped } from './cappedBody.js';

export const fetchImpl = () => globalThis.fetch?.bind(globalThis);
// A 30x under `redirect: 'manual'`: opaque (status 0) in a browser, the bare status under Node.
const isRedirect = (resp) => resp.type === 'opaqueredirect' || (resp.status >= 300 && resp.status < 400 && resp.status !== 304);

const req = async (conn, method, path, opts = {}) => {
  const { body, raw, query, headers: extra, fetch: f = fetchImpl(), retried = false } = opts;
  let url = conn.url + path;
  if (query) url += '?' + new URLSearchParams(query).toString();
  const headers = { ...extra, Authorization: 'Bearer ' + conn.token };
  let payload = body;
  if (body != null && !raw) { headers['Content-Type'] = 'application/json'; payload = JSON.stringify(body); }
  const resp = await f(url, { method, headers, body: payload, redirect: 'manual' });
  // Refused, never followed, so the bearer never reaches the host a 30x names.
  if (isRedirect(resp)) throw Object.assign(new Error(`${method} ${path}: the server redirected — connect to its final address`), { status: resp.status });
  if (resp.status === 304) return raw ? resp : null;
  if (!resp.ok) {
    // A session token dies with a server restart: re-mint from the credential once.
    if (!retried && (resp.status === 401 || resp.status === 403)
        && conn.credential && path !== '/auth/token') {
      const r = await req({ url: conn.url, token: conn.credential }, 'POST', '/auth/token',
        { body: {}, fetch: f, retried: true });
      conn.token = r.token;
      const out = await req(conn, method, path, { ...opts, fetch: f, retried: true });
      // It minted AND the fresh session works: the credential is an admin token.
      conn.credentialKind = 'admin';
      return out;
    }
    let msg = `HTTP ${resp.status}`;
    try { const e = await readJsonCapped(resp, MAX_ERROR_BYTES); if (e && e.message) msg = e.message; } catch { /* non-JSON */ }
    const err = new Error(`${method} ${path}: ${msg}`);
    err.status = resp.status;   // the connect() admin-mint fallback keys on this
    throw err;
  }
  if (resp.status === 204) return null;
  if (raw) return resp;
  return readJsonCapped(resp);
};

// Proves a session without listing the server: GET /auth/session, or one project from a
// server that predates it (404).
export const checkSession = async (conn, f = fetchImpl()) => {
  try {
    await req(conn, 'GET', '/auth/session', { fetch: f });
  } catch (err) {
    if (err.status !== 404) throw err;
    await req(conn, 'GET', '/projects', { query: { limit: 1 }, fetch: f });
  }
};

// An invite link's `#token=` fragment is the credential unless an explicit token is given.
export const connect = async (rawUrl, token = '', f = fetchImpl()) => {
  const inv = parseInviteUrl(rawUrl);
  const url = normalizeUrl(inv.url);
  const supplied = token || inv.token;
  let tok = supplied;
  // '' until the mint round below proves the supplied value is an admin token.
  let kind = '';
  if (!tok) {
    const r = await req({ url, token: '' }, 'POST', '/auth/token', { body: {}, fetch: f });
    tok = r.token;
  } else {
    try {
      await checkSession({ url, token: tok }, f);
    } catch (err) {
      // The pasted value may be the ADMIN token: it can't list projects, but it can MINT.
      if (err.status !== 401 && err.status !== 403) throw err;
      const r = await req({ url, token: tok }, 'POST', '/auth/token', { body: {}, fetch: f });
      tok = r.token;
      await checkSession({ url, token: tok }, f);
      kind = 'admin';
    }
  }
  // The credential outlives server restarts: req() re-mints with it when a session goes stale.
  return { url, token: tok, credential: supplied || '', credentialKind: kind };
};

// A server that keeps handing out cursors is cut off here (the cli's max_pages), never followed forever.
export const MAX_LIST_PAGES = 1000;

// The cursor a page names for the next one, '' on the last; a repeat, or one past MAX_LIST_PAGES, throws.
const nextCursor = (body, seen) => {
  const next = body && typeof body.nextCursor === 'string' ? body.nextCursor : '';
  if (!next) return '';
  if (seen.has(next)) throw new Error('GET /projects: the server handed back the same page cursor twice');
  if (seen.size + 1 >= MAX_LIST_PAGES) throw new Error(`GET /projects: kept paging past ${MAX_LIST_PAGES} pages`);
  seen.add(next);
  return next;
};

// Every project: each `nextCursor` goes back as `after` until a page names none.
export const listProjects = async (conn, f = fetchImpl()) => {
  const out = [];
  const seen = new Set();
  let after = '';
  do {
    const r = await req(conn, 'GET', '/projects', { query: after ? { after } : undefined, fetch: f });
    out.push(...((r && r.projects) || []));
    after = nextCursor(r, seen);
  } while (after);
  return out;
};

// Every project, `limit` a page; `prev` (the last answer) sends each page's ETag back, so an unchanged
// page costs a 304 and an unchanged list answers `changed: false` with no projects.
export const SHARED_LIST_LIMIT = 200;
export const listProjectsIfChanged = async (conn, prev = null, f = fetchImpl()) => {
  const old = (prev && prev.pages) || [];
  const pages = [];
  const seen = new Set();
  let after = '';
  do {
    const was = old[pages.length] && old[pages.length].after === after ? old[pages.length] : null;
    const resp = await req(conn, 'GET', '/projects', {
      raw: true, query: after ? { limit: SHARED_LIST_LIMIT, after } : { limit: SHARED_LIST_LIMIT },
      headers: was && was.etag ? { 'If-None-Match': was.etag } : undefined, fetch: f,
    });
    let page = resp.status === 304 ? was : null;
    if (!page) {
      const r = await readJsonCapped(resp);
      page = { after, etag: resp.headers?.get?.('etag') || '', projects: (r && r.projects) || [], next: r && r.nextCursor };
    }
    pages.push(page);
    after = nextCursor({ nextCursor: page.next }, seen);
  } while (after);
  const changed = pages.length !== old.length || pages.some((p, i) => p !== old[i]);
  return { changed, etag: pages[0].etag, pages, projects: changed ? pages.flatMap((p) => p.projects) : null };
};

export const createProject = async (conn, { name, source = '', resource = '' }, f = fetchImpl()) =>
  req(conn, 'POST', '/projects', { body: { name, source, resource, hasImage: true }, fetch: f });

// kind: 'original' (unedited; the editor re-applies filter/lines) | 'result' (baked preview).
// A server's file is bytes from outside like any page's: read under the one fetch cap.
export const fetchProjectImage = async (conn, projectId, kind = 'original', f = fetchImpl()) => {
  const resp = await req(conn, 'GET', `/projects/${projectId}/files/${kind}`, { raw: true, fetch: f });
  return readBlobCapped(resp);
};


// Bearer-authed JSON over server/internal/protocol; a stale session token is re-minted once.
import { normalizeUrl, parseInviteUrl } from './connectionModel.js';

export const fetchImpl = () => globalThis.fetch?.bind(globalThis);

const req = async (conn, method, path, { body, raw, query, fetch: f = fetchImpl(), retried = false } = {}) => {
  let url = conn.url + path;
  if (query) url += '?' + new URLSearchParams(query).toString();
  const headers = { Authorization: 'Bearer ' + conn.token };
  let payload = body;
  if (body != null && !raw) { headers['Content-Type'] = 'application/json'; payload = JSON.stringify(body); }
  const resp = await f(url, { method, headers, body: payload });
  if (!resp.ok) {
    // A session token dies with a server restart: re-mint from the credential once.
    if (!retried && (resp.status === 401 || resp.status === 403)
        && conn.credential && path !== '/auth/token') {
      const r = await req({ url: conn.url, token: conn.credential }, 'POST', '/auth/token',
        { body: {}, fetch: f, retried: true });
      conn.token = r.token;
      const out = await req(conn, method, path, { body, raw, query, fetch: f, retried: true });
      // It minted AND the fresh session works: the credential is an admin token.
      conn.credentialKind = 'admin';
      return out;
    }
    let msg = `HTTP ${resp.status}`;
    try { const e = await resp.json(); if (e && e.message) msg = e.message; } catch { /* non-JSON */ }
    const err = new Error(`${method} ${path}: ${msg}`);
    err.status = resp.status;   // the connect() admin-mint fallback keys on this
    throw err;
  }
  if (resp.status === 204) return null;
  if (raw) return resp;
  return resp.json();
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
      await req({ url, token: tok }, 'GET', '/projects', { fetch: f });
    } catch (err) {
      // The pasted value may be the ADMIN token: it can't list projects, but it can MINT.
      if (err.status !== 401 && err.status !== 403) throw err;
      const r = await req({ url, token: tok }, 'POST', '/auth/token', { body: {}, fetch: f });
      tok = r.token;
      await req({ url, token: tok }, 'GET', '/projects', { fetch: f });
      kind = 'admin';
    }
  }
  // The credential outlives server restarts: req() re-mints with it when a session goes stale.
  return { url, token: tok, credential: supplied || '', credentialKind: kind };
};

export const listProjects = async (conn, f = fetchImpl()) => {
  const r = await req(conn, 'GET', '/projects', { fetch: f });
  return r.projects || [];
};

export const createProject = async (conn, { name, source = '', resource = '' }, f = fetchImpl()) =>
  req(conn, 'POST', '/projects', { body: { name, source, resource, hasImage: true }, fetch: f });

// kind: 'original' (unedited; the editor re-applies filter/lines) | 'result' (baked preview).
export const fetchProjectImage = async (conn, projectId, kind = 'original', f = fetchImpl()) => {
  const resp = await req(conn, 'GET', `/projects/${projectId}/files/${kind}`, { raw: true, fetch: f });
  return resp.blob();
};


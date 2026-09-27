// A server that pages GET /projects: every listing follows `nextCursor` to the last page, so it sees
// every project as it did before paging existed, and a cursor that loops throws instead of truncating.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  listProjects, listProjectsIfChanged, refreshSharedPins, SHARED_LIST_LIMIT,
} from '../../../src/lib/connection/connections.js';
import { MAX_LIST_PAGES } from '../../../src/lib/connection/rest.js';

const json = (status, body, headers = {}) => ({
  ok: status >= 200 && status < 300, status, json: async () => body,
  headers: { get: (k) => headers[k.toLowerCase()] ?? null },
});
const conn = { url: 'http://s:1', token: 'tok' };
const proj = (id) => ({ id, name: id, hasImage: true, source: `http://img/${id}.png` });

// pages[i] answers `after = cursor(i)`; each page's ETag carries its own version, bumped by `touch`.
const pagingServer = (pages, { next = (i) => (i + 1 < pages.length ? `c${i + 1}` : undefined) } = {}) => {
  const log = [];
  const version = pages.map(() => 1);
  const f = async (url, init = {}) => {
    const u = new URL(url);
    const query = Object.fromEntries(u.searchParams);
    log.push({ query, ifNoneMatch: (init.headers || {})['If-None-Match'] });
    const i = query.after ? Number(query.after.slice(1)) : 0;
    const etag = `"p${i}v${version[i]}"`;
    if ((init.headers || {})['If-None-Match'] === etag) return json(304, null);
    const body = { projects: pages[Math.min(i, pages.length - 1)].map(proj) };
    const cursor = next(i);
    if (cursor !== undefined) body.nextCursor = cursor;
    return json(200, body, { etag });
  };
  return { f, log, touch: (i, ids) => { pages[i] = ids; version[i]++; } };
};

test('listProjects walks every page in order, asking the first exactly as before', async () => {
  const { f, log } = pagingServer([['a', 'b'], ['c'], ['d']]);
  assert.deepEqual((await listProjects(conn, f)).map((p) => p.id), ['a', 'b', 'c', 'd']);
  assert.deepEqual(log.map((r) => r.query), [{}, { after: 'c1' }, { after: 'c2' }]);
});

test('a server that does not page is one request with no query, as before', async () => {
  const { f, log } = pagingServer([['a']]);
  assert.deepEqual((await listProjects(conn, f)).map((p) => p.id), ['a']);
  assert.deepEqual(log.map((r) => r.query), [{}]);
});

test('a cursor handed back twice, or one past the page cap, throws rather than loop', async () => {
  const loop = pagingServer([['a'], ['b']], { next: () => 'c1' });
  await assert.rejects(() => listProjects(conn, loop.f), /same page cursor twice/);
  assert.equal(loop.log.length, 2);
  const endless = pagingServer([['a'], ['b']], { next: (i) => `c${i + 1}` });
  await assert.rejects(() => listProjects(conn, endless.f), /past 1000 pages/);
  assert.equal(endless.log.length, MAX_LIST_PAGES);
});

test('the shared list walks every bounded page, and each page rides its own ETag', async () => {
  const server = pagingServer([['a', 'b'], ['c'], ['d']]);
  const first = await listProjectsIfChanged(conn, null, server.f);
  assert.deepEqual(first.projects.map((p) => p.id), ['a', 'b', 'c', 'd']);
  const limit = String(SHARED_LIST_LIMIT);
  assert.deepEqual(server.log.map((r) => r.query), [{ limit }, { limit, after: 'c1' }, { limit, after: 'c2' }]);
  const same = await listProjectsIfChanged(conn, first, server.f);
  assert.deepEqual([same.changed, same.projects], [false, null]);
  assert.deepEqual(server.log.slice(3).map((r) => r.ifNoneMatch), ['"p0v1"', '"p1v1"', '"p2v1"']);
  // A change on a later page is news even while the first page answers 304.
  server.touch(2, []);
  const moved = await listProjectsIfChanged(conn, same.changed ? same : first, server.f);
  assert.equal(moved.changed, true);
  assert.deepEqual(moved.projects.map((p) => p.id), ['a', 'b', 'c']);
});

test('shared pins cover every page; a looping server is skipped, never shown cut short', async () => {
  const paged = pagingServer([['a'], ['b'], ['c']]);
  const pins = await refreshSharedPins([conn], new Map(), paged.f);
  assert.deepEqual(pins.pins.map((p) => p.projectId), ['a', 'b', 'c']);
  const looping = pagingServer([['a'], ['b']], { next: () => 'c1' });
  assert.deepEqual((await refreshSharedPins([conn], new Map(), looping.f)).pins, []);
});

test('a server reply past the byte cap is refused, a page and an error body alike', async () => {
  const endless = () => new ReadableStream({ pull(c) { c.enqueue(new Uint8Array(1 << 20)); } });
  const flood = async () => new Response(endless(), { status: 200 });
  await assert.rejects(() => listProjectsIfChanged(conn, null, flood), /67108864-byte fetch cap/);
  const loud = async () => new Response(endless(), { status: 500 });
  await assert.rejects(() => listProjects(conn, loud), /HTTP 500/);
});

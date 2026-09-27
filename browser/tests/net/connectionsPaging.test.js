// A server that pages GET /projects (js/net/serverConnection.js): the listing follows `nextCursor`
// to the last page, so it sees every project as before paging existed, and a looping cursor throws.
import { test } from 'node:test';
import assert from 'node:assert';
import { ServerConnection, MAX_LIST_PAGES } from '../../js/net/serverConnection.js';

// pages[i] answers `?after=c<i>`; `next(i)` is the cursor page i names (undefined on the last).
const pagingFetch = (pages, next = (i) => (i + 1 < pages.length ? `c${i + 1}` : undefined)) => {
  const log = [];
  const fetchImpl = async (url) => {
    const u = new URL(url);
    log.push(u.search);
    const after = u.searchParams.get('after');
    const i = after ? Number(after.slice(1)) : 0;
    const body = { projects: pages[Math.min(i, pages.length - 1)].map((id) => ({ id, name: id })) };
    if (next(i) !== undefined) body.nextCursor = next(i);
    return { ok: true, status: 200, json: async () => body };
  };
  return { log, conn: new ServerConnection('http://a:1', { token: 't', fetchImpl }) };
};

test('listProjects walks every page in order, tagging each project remote', async () => {
  const { log, conn } = pagingFetch([['a', 'b'], ['c'], ['d']]);
  const list = await conn.listProjects();
  assert.deepEqual(list.map((p) => p.id), ['a', 'b', 'c', 'd']);
  assert.ok(list.every((p) => p.remote === true && p.serverUrl === 'http://a:1'));
  assert.deepEqual(log, ['', '?after=c1', '?after=c2']);
});

test('a server that does not page is one request with no query, as before', async () => {
  const { log, conn } = pagingFetch([['a']]);
  assert.deepEqual((await conn.listProjects()).map((p) => p.id), ['a']);
  assert.deepEqual(log, ['']);
});

test('a cursor handed back twice, or one past the page cap, throws rather than loop', async () => {
  const loop = pagingFetch([['a'], ['b']], () => 'c1');
  await assert.rejects(() => loop.conn.listProjects(), /same page cursor twice/);
  assert.equal(loop.log.length, 2);
  const endless = pagingFetch([['a'], ['b']], (i) => `c${i + 1}`);
  await assert.rejects(() => endless.conn.listProjects(), /past 1000 pages/);
  assert.equal(endless.log.length, MAX_LIST_PAGES);
});

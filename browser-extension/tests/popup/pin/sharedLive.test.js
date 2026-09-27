// popup/pin/sharedLive.js — which refresh keeps the shared rows current: a server's live feed,
// or the panel clock stretching while nothing changes. Clock, timers and sockets are stand-ins.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createSharedLive, MAX_STRIDE, EVENT_DEBOUNCE_MS } from '../../../src/popup/pin/sharedLive.js';

const harness = ({ conns = [{ url: 'http://a:1', token: 't' }], changes = [] } = {}) => {
  const jobs = new Set();
  const clock = { add: (j) => jobs.add(j), remove: (j) => jobs.delete(j) };
  const pending = new Map();
  let nextId = 1;
  const timers = {
    setTimeout: (fn, ms) => { pending.set(nextId, { fn, ms }); return nextId++; },
    clearTimeout: (id) => pending.delete(id),
  };
  const subs = [];
  const subscribe = (conn, onEvent, { onState }) => {
    const sub = { conn, onEvent, onState, closed: false, close() { sub.closed = true; } };
    subs.push(sub);
    return sub;
  };
  let refreshes = 0;
  const refresh = async () => { refreshes++; return changes.length ? changes.shift() : false; };
  const live = createSharedLive({ refresh, getConnections: () => conns, clock, subscribe, timers });
  const flushTimers = async () => {
    for (const [id, { fn }] of [...pending]) { pending.delete(id); fn(); }
    await new Promise((r) => setImmediate(r));
  };
  return { live, jobs, subs, pending, flushTimers, refreshes: () => refreshes, conns };
};

test('start opens one feed per server and joins the panel clock', () => {
  const h = harness({ conns: [{ url: 'http://a:1', token: 't' }, { url: 'http://b:2', token: 't' }] });
  h.live.start();
  assert.deepEqual(h.subs.map((s) => s.conn.url), ['http://a:1', 'http://b:2']);
  assert.equal(h.jobs.size, 1);
  h.live.start();
  assert.equal(h.subs.length, 2, 'a second start opens nothing new');
});

test('while every server has a live feed the clock tick sends nothing', async () => {
  const h = harness();
  h.live.start();
  h.subs[0].onState(true);
  for (let i = 0; i < 5; i++) await h.live.tick();
  assert.equal(h.refreshes(), 0);
});

test('a burst of project events becomes one debounced refresh', async () => {
  const h = harness();
  h.live.start();
  h.subs[0].onState(true);
  for (let i = 0; i < 5; i++) h.subs[0].onEvent({ type: 'project-event', event: 'updated' });
  assert.equal(h.pending.size, 1);
  assert.equal([...h.pending.values()][0].ms, EVENT_DEBOUNCE_MS);
  await h.flushTimers();
  assert.equal(h.refreshes(), 1);
});

test('with no feed an unchanged list is polled ever more rarely, a change snaps it back', async () => {
  const h = harness({ changes: [false, false, false, false, false, true] });
  h.live.start();
  h.subs[0].onState(false);   // the server refused the socket
  const polledAt = [];
  for (let tick = 1; tick <= 40; tick++) {
    const before = h.refreshes();
    await h.live.tick();
    if (h.refreshes() > before) polledAt.push(tick);
  }
  // 8 s, then 16, 32 and 64 s apart; the change at the 6th poll resets the stride to one tick.
  assert.deepEqual(polledAt.slice(0, 7), [1, 3, 7, 15, 23, 31, 32]);
  assert.equal(MAX_STRIDE, 8);
});

test('a feed that drops is re-opened on the next tick, which polls at once to catch up', async () => {
  const h = harness({ changes: [false, false, false, false] });
  h.live.start();
  h.subs[0].onState(false);
  for (let i = 0; i < 3; i++) await h.live.tick();
  assert.equal(h.live.stride, 4, 'stretched while the socket stayed down');
  const reopened = h.subs.at(-1);
  reopened.onState(true);
  reopened.onState(false);   // a working feed is lost
  const before = h.refreshes();
  await h.live.tick();
  assert.equal(h.refreshes(), before + 1, 'no waiting out the stretched stride');
  assert.notEqual(h.subs.at(-1), reopened, 'a fresh socket was opened');
});

test('a changed token or a removed server closes its feed; stop closes all and leaves the clock', () => {
  const h = harness();
  h.live.start();
  const first = h.subs[0];
  h.conns[0] = { url: 'http://a:1', token: 'fresh' };
  h.live.start();
  assert.ok(first.closed);
  assert.equal(h.subs.at(-1).conn.token, 'fresh');
  h.live.stop();
  assert.ok(h.subs.at(-1).closed);
  assert.equal(h.jobs.size, 0);
  h.conns.length = 0;
  h.live.start();
  assert.equal(h.jobs.size, 0, 'no servers, no clock job');
});

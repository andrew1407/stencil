// Keeps a panel's shared rows fresh. Each server's /ws project feed refreshes the list on a
// change; while any server has no live feed the panel clock polls, stretching to every 8th
// tick while nothing changes, and a feed that drops catches up on the next tick.
import { subscribeProjectEvents } from '../../lib/connection/events.js';
import { pollClock } from '../../lib/pollClock.js';

export const MAX_STRIDE = 8;          // ticks: 8 s → 64 s between unchanged polls
export const EVENT_DEBOUNCE_MS = 250; // one refresh for a burst of edits

export const createSharedLive = ({
  refresh, getConnections, clock = pollClock, subscribe = subscribeProjectEvents, timers = globalThis,
}) => {
  const feeds = new Map();   // server url → { token, live, sub }
  let stride = 1;
  let wait = 0;
  let debounce = null;
  let running = null;
  let again = false;
  const connections = () => getConnections() || [];

  // Coalesced: a request landing mid-refresh runs one more pass instead of a second in parallel.
  const run = () => {
    if (running) { again = true; return running; }
    running = (async () => {
      let changed = false;
      do { again = false; changed = (await refresh()) || changed; } while (again);
      return changed;
    })().finally(() => { running = null; });
    return running;
  };

  const onEvent = () => {
    if (debounce !== null) return;
    debounce = timers.setTimeout(() => { debounce = null; run().catch(() => {}); }, EVENT_DEBOUNCE_MS);
  };

  const open = (conn) => {
    const feed = { token: conn.token, live: false, sub: null };
    feeds.set(conn.url, feed);
    feed.sub = subscribe(conn, onEvent, {
      onState: (live) => {
        if (feeds.get(conn.url) !== feed) return;
        if (live) { feed.live = true; return; }
        feeds.delete(conn.url);
        if (feed.live) { stride = 1; wait = 0; }
      },
    });
    if (!feed.sub && feeds.get(conn.url) === feed) feeds.delete(conn.url);
  };

  const syncFeeds = () => {
    const want = new Map(connections().map((c) => [c.url, c]));
    for (const [url, feed] of [...feeds]) {
      const c = want.get(url);
      if (!c || c.token !== feed.token) { feed.sub?.close(); feeds.delete(url); }
    }
    for (const [url, c] of want) if (!feeds.has(url)) open(c);
  };

  const allLive = () => {
    const list = connections();
    return list.length > 0 && list.every((c) => feeds.get(c.url)?.live);
  };

  const tick = async () => {
    if (allLive()) return;
    if (wait > 0) { wait--; return; }
    syncFeeds();
    let changed = true;
    try { changed = await run(); } catch { /* the next tick retries */ }
    stride = changed ? 1 : Math.min(stride * 2, MAX_STRIDE);
    wait = stride - 1;
  };

  const stop = () => {
    clock.remove(tick);
    for (const feed of feeds.values()) feed.sub?.close();
    feeds.clear();
    if (debounce !== null) { timers.clearTimeout(debounce); debounce = null; }
    stride = 1;
    wait = 0;
  };

  return {
    start: () => {
      if (!connections().length) { stop(); return; }
      syncFeeds();
      clock.add(tick);
    },
    stop,
    tick,
    get liveFeeds() { return [...feeds.values()].filter((f) => f.live).length; },
    get stride() { return stride; },
  };
};

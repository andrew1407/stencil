import { test } from 'node:test';
import assert from 'node:assert';

import { createRemoteListing, showsRemoteSkeletons } from '../js/core/remoteListing.js';

// The projects modal's server-listing cache — the state machine behind the two
// shimmer skeleton rows. The regression pinned here (user screenshot): after
// "Clear All Local", two skeletons stayed on screen forever. The wipe deferred a
// connections-changed invalidate into a stale-fetch race; the dropped stale fetch
// left `loading` latched true, ensure() then refused to start a fresh fetch, the
// cache stayed null, and render() painted skeletons no fetch would ever fill.

const tick = () => new Promise((r) => setTimeout(r, 0));
const skeletons = (l) => showsRemoteSkeletons({
  showServer: true, hasServers: true, cache: l.cache, loading: l.loading,
});

// ── Skeletons only while a fetch is actually in flight ──────────────────────

test('showsRemoteSkeletons: only a LIVE fetch earns skeletons', () => {
  assert.strictEqual(showsRemoteSkeletons({ showServer: true, hasServers: true, cache: null, loading: true }), true);
  // The immortal-skeleton pin: a null cache with NO fetch running renders the
  // honest empty/error state, never placeholders nothing will fill.
  assert.strictEqual(showsRemoteSkeletons({ showServer: true, hasServers: true, cache: null, loading: false }), false);
  assert.strictEqual(showsRemoteSkeletons({ showServer: true, hasServers: true, cache: [], loading: false }), false,
    'a settled (even empty) listing shows rows or the empty state');
  assert.strictEqual(showsRemoteSkeletons({ showServer: true, hasServers: true, cache: [], loading: true }), false,
    'a cached listing never regresses to skeletons');
  assert.strictEqual(showsRemoteSkeletons({ showServer: false, hasServers: true, cache: null, loading: true }), false,
    'local-only filter shows no server skeletons');
  assert.strictEqual(showsRemoteSkeletons({ showServer: true, hasServers: false, cache: null, loading: true }), false,
    'no connected servers, nothing to wait for');
  assert.strictEqual(showsRemoteSkeletons(), false, 'defaults are all off');
});

// ── The fetch lifecycle ─────────────────────────────────────────────────────

test('ensure: one fetch fills the cache, skeletons resolve, done fires', async () => {
  const fetches = [];
  const listing = createRemoteListing(() => new Promise((res, rej) => fetches.push({ res, rej })));
  let done = 0;
  assert.strictEqual(skeletons(listing), false, 'nothing pending before the first ensure');
  listing.ensure(() => done++);
  assert.strictEqual(listing.loading, true);
  assert.strictEqual(skeletons(listing), true, 'skeletons while the fetch is live');
  listing.ensure(() => done++);   // a second ensure while loading must NOT double-fetch
  assert.strictEqual(fetches.length, 1, 'one fetch per cycle');
  fetches[0].res([{ id: 'p1' }]);
  await tick();
  assert.deepStrictEqual(listing.cache, [{ id: 'p1' }]);
  assert.strictEqual(listing.loading, false);
  assert.strictEqual(done, 1, 'done runs once, for the deferred re-render');
  assert.strictEqual(skeletons(listing), false, 'skeletons gone once it settles');
  listing.ensure(() => done++);   // cached → no refetch, no skeletons
  assert.strictEqual(fetches.length, 1);
});

test('a FAILED fetch settles to the error state — never immortal skeletons', async () => {
  const fetches = [];
  const listing = createRemoteListing(() => new Promise((res, rej) => fetches.push({ res, rej })));
  let done = 0;
  listing.ensure(() => done++);
  fetches[0].rej(new Error('unreachable'));
  await tick();
  assert.deepStrictEqual(listing.cache, [], 'failed → loaded-empty, so rows stop waiting');
  assert.strictEqual(listing.failed, true, 'render shows "Could not reach server."');
  assert.strictEqual(listing.loading, false);
  assert.strictEqual(done, 1);
  assert.strictEqual(skeletons(listing), false, 'a failed fetch leaves no skeletons behind');
});

// ── The Clear All regression ────────────────────────────────────────────────

test('invalidate mid-flight unlatches loading; the dropped stale fetch changes nothing', async () => {
  const fetches = [];
  const listing = createRemoteListing(() => new Promise((res, rej) => fetches.push({ res, rej })));
  listing.ensure();                        // modal open: fetch #1 in flight
  listing.invalidate();                    // connections-changed during the removal wipe
  assert.strictEqual(listing.loading, false,
    'invalidate must clear the latch — the old code left it true forever');
  fetches[0].res([{ id: 'stale' }]);       // fetch #1 lands late, one token behind
  await tick();
  assert.strictEqual(listing.cache, null, 'the stale result is dropped');
  assert.strictEqual(listing.loading, false, 'and dropping it does not re-latch loading');
  assert.strictEqual(listing.failed, false);
});

test('after Clear All settles, skeletons resolve instead of sticking', async () => {
  // The full screenshot timeline, at logic level: fetch #1 → invalidate mid-wipe →
  // stale drop → the settle render's ensure() must start a FRESH fetch (this is the
  // step the latched flag starved), whose resolution finally clears the skeletons.
  const fetches = [];
  const listing = createRemoteListing(() => new Promise((res, rej) => fetches.push({ res, rej })));
  listing.ensure();                        // fetch #1
  listing.invalidate();                    // wipe defers the render; cache nulled
  fetches[0].res([{ id: 'stale' }]);
  await tick();
  listing.ensure();                        // the settle render after the wipe
  assert.strictEqual(fetches.length, 2, 'a fresh fetch starts — skeletons are being fed again');
  assert.strictEqual(skeletons(listing), true, 'skeletons shown only while fetch #2 is live');
  fetches[1].res([]);
  await tick();
  assert.deepStrictEqual(listing.cache, []);
  assert.strictEqual(skeletons(listing), false, 'skeletons gone once the listing settles');
});

test('a stale FAILURE is dropped just like a stale success', async () => {
  const fetches = [];
  const listing = createRemoteListing(() => new Promise((res, rej) => fetches.push({ res, rej })));
  listing.ensure();
  listing.invalidate();
  fetches[0].rej(new Error('old cycle died'));
  await tick();
  assert.strictEqual(listing.cache, null, 'a stale failure must not fake a loaded-empty listing');
  assert.strictEqual(listing.failed, false, 'nor flash the error state for a dead cycle');
  assert.strictEqual(listing.loading, false);
  // …and the next cycle is healthy.
  listing.ensure();
  fetches[1].res([{ id: 'fresh' }]);
  await tick();
  assert.deepStrictEqual(listing.cache, [{ id: 'fresh' }]);
});

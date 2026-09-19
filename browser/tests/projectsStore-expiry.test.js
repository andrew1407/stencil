// js/core/projectsStore.js expiry: the period presets, the stored expiresAt, the
// expiring-soon window, renewal and the sweep. Split from projectsStore.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import {
  ProjectsStore, shouldPersist, baseProjectName, EXPIRY_MS, WARN_MS,
  REGISTRY_KEY, PROJECT_PREFIX, MIGRATED_FLAG,
  periodMs, addPeriod, PERIOD_MS, DEFAULT_PERIOD, normalizeKeywords,
} from '../js/core/projectsStore.js';
import { createMemoryStorage } from './helpers/memoryStorage.js';

// Map-backed localStorage shim (shared helper): exposes keys() for the store's
// enumeration, and `throwOnSet` for the QuotaExceededError path.
const makeShim = (opts = {}) => createMemoryStorage({}, opts);

const meta = (id, over = {}) => ({
  id, name: over.name ?? id, thumbnail: null,
  createdAt: over.createdAt ?? 1000, updatedAt: over.updatedAt ?? 1000,
  hasImage: false, imageW: null, imageH: null, ...over,
});

test('periodMs / addPeriod presets (fixed durations); mirror core', () => {
  const DAY = 24 * 60 * 60 * 1000;
  assert.strictEqual(periodMs('day'), DAY);
  assert.strictEqual(periodMs('week'), 7 * DAY);
  assert.strictEqual(periodMs('week'), EXPIRY_MS);
  assert.strictEqual(periodMs('fortnight'), 14 * DAY);
  assert.strictEqual(periodMs('month'), 30 * DAY);
  assert.strictEqual(periodMs('3month'), 90 * DAY);
  assert.strictEqual(periodMs('6month'), 180 * DAY);
  assert.strictEqual(periodMs('year'), 365 * DAY);
  // Unknown / empty → one week.
  assert.strictEqual(periodMs(''), 7 * DAY);
  assert.strictEqual(periodMs('decade'), 7 * DAY);
  assert.strictEqual(addPeriod(1000, 'day'), 1000 + DAY);
  assert.strictEqual(PERIOD_MS.week, EXPIRY_MS);
});

test('expiry keyed on stored expiresAt; 0/absent == keep forever', () => {
  const s = new ProjectsStore(makeShim());
  const now = 1_000_000_000;
  assert.strictEqual(s.isExpired(meta('a', { expiresAt: now + 1000 }), now), false);
  assert.strictEqual(s.isExpired(meta('a', { expiresAt: now - 1 }), now), true);
  assert.strictEqual(s.isExpired(meta('a', { expiresAt: 0 }), now), false); // keep forever
  assert.strictEqual(s.expiresAt(meta('a', { expiresAt: now + 1000 })), now + 1000);
  assert.strictEqual(s.expiresAt(meta('a', { expiresAt: 0 })), null); // keep forever
});

test('isExpiringSoon: true within WARN_MS of expiry, false when further out', () => {
  const s = new ProjectsStore(makeShim());
  const now = 1_000_000_000;
  const soon = meta('a', { expiresAt: now + (WARN_MS / 2) });
  assert.strictEqual(s.isExpiringSoon(soon, now), true);
  const later = meta('b', { expiresAt: now + (2 * WARN_MS) });
  assert.strictEqual(s.isExpiringSoon(later, now), false);
  // Keep forever → never "soon".
  assert.strictEqual(s.isExpiringSoon(meta('c', { expiresAt: 0 }), now), false);
});

test('isExpiringSoon: false once already expired (gets the expired treatment)', () => {
  const s = new ProjectsStore(makeShim());
  const now = 1_000_000_000;
  const expired = meta('a', { expiresAt: now - 1 });
  assert.strictEqual(s.isExpired(expired, now), true);
  assert.strictEqual(s.isExpiringSoon(expired, now), false);
});

test('isExpiringSoon boundary: true at exactly WARN_MS remaining, false just past', () => {
  const s = new ProjectsStore(makeShim());
  const now = 1_000_000_000;
  const edge = meta('a', { expiresAt: now + WARN_MS });
  assert.strictEqual(s.isExpiringSoon(edge, now), true);
  const justOut = meta('b', { expiresAt: now + WARN_MS + 1 });
  assert.strictEqual(s.isExpiringSoon(justOut, now), false);
});

test('setExpiration sets fields exactly, no updatedAt bump', () => {
  const s = new ProjectsStore(makeShim());
  s.upsert(meta('a', { updatedAt: 5000 }), { image: null, layout: {} });
  const updatedAt = s.getMeta('a').updatedAt;
  const m = s.setExpiration('a', { expiresAt: 9999, refreshPeriod: 'month', autoRefresh: false });
  assert.strictEqual(m.expiresAt, 9999);
  assert.strictEqual(m.refreshPeriod, 'month');
  assert.strictEqual(m.autoRefresh, false);
  assert.strictEqual(s.getMeta('a').updatedAt, updatedAt);
  // Empty period normalises to the default; keep-forever via 0.
  s.setExpiration('a', { expiresAt: 0, refreshPeriod: '' });
  assert.strictEqual(s.getMeta('a').refreshPeriod, DEFAULT_PERIOD);
  assert.strictEqual(s.getMeta('a').expiresAt, 0);
  assert.strictEqual(s.setExpiration('missing', { expiresAt: 1 }), null);
});

test('renew sets expiresAt = now + refresh period (not updatedAt)', () => {
  const s = new ProjectsStore(makeShim());
  s.upsert(meta('a', { refreshPeriod: 'month', expiresAt: 1 }), { image: null, layout: {} });
  const now = 1_000_000_000;
  const renewed = s.renew('a', now);
  assert.strictEqual(renewed.expiresAt, now + periodMs('month'));
  assert.strictEqual(s.isExpired(s.getMeta('a'), now), false);
});

test('renew returns null for a missing project', () => {
  const s = new ProjectsStore(makeShim());
  assert.strictEqual(s.renew('nope', 123), null);
});

test('sweepExpired removes only expired, keeps keep-forever, returns ids', () => {
  const shim = makeShim();
  const s = new ProjectsStore(shim);
  const now = 10 * EXPIRY_MS;
  s.upsert(meta('fresh', { expiresAt: now + EXPIRY_MS }), { image: null, layout: {} });
  s.upsert(meta('old', { expiresAt: now - 5000 }), { image: null, layout: {} });
  s.upsert(meta('keep', { expiresAt: 0 }), { image: null, layout: {} }); // keep forever
  const removed = s.sweepExpired(now);
  assert.deepStrictEqual(removed, ['old']);
  assert.ok(s.getMeta('fresh'));
  assert.ok(s.getMeta('keep'));
  assert.strictEqual(s.getMeta('old'), null);
});

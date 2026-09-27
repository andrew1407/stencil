// js/llm/sessionKey.js — the anthropic session key (llm-providers.md §5): sessionStorage only, the
// providers.json TTL, dropped on expiry and on forget, kept across a reload of the same tab, absent
// in a new one, and inert wherever the storage is missing or refuses.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import PROVIDERS_ASSET from '../../js/config/llm/providers.json' with { type: 'json' };
import { createMemoryStorage, installMemoryStorage } from '../helpers/memoryStorage.js';

const local = installMemoryStorage();
// A tab's sessionStorage, swappable: Node ships its own global, which a test must not share.
const useSession = (storage) => Object.defineProperty(globalThis, 'sessionStorage', { value: storage, configurable: true, writable: true });
let tab = createMemoryStorage();
useSession(tab);

const store = await import('../../js/llm/sessionKey.js');
const { SESSION_KEY_ITEM, SESSION_KEY_TTL_MS, readSessionKey, sessionKey, setSessionKey, forgetSessionKey } = store;
const KEY = 'sk-ant-test-0123456789abcdef';
const T0 = Date.UTC(2026, 8, 27, 9, 0);

const freshTab = () => { tab = createMemoryStorage(); useSession(tab); };

test('the TTL is providers.json anthropic.sessionKey.ttlMinutes', () => {
  assert.equal(SESSION_KEY_TTL_MS, PROVIDERS_ASSET.providers.anthropic.sessionKey.ttlMinutes * 60_000);
  assert.equal(SESSION_KEY_TTL_MS, 12 * 3_600_000);
});

test('a set key is held as { key, expiresAt } in sessionStorage and never in localStorage', () => {
  freshTab();
  assert.equal(setSessionKey(`  ${KEY}\n`, T0), T0 + SESSION_KEY_TTL_MS);
  assert.deepEqual(JSON.parse(tab.getItem(SESSION_KEY_ITEM)), { key: KEY, expiresAt: T0 + SESSION_KEY_TTL_MS });
  assert.deepEqual(readSessionKey(T0 + 1), { key: KEY, expiresAt: T0 + SESSION_KEY_TTL_MS });
  assert.equal(sessionKey(T0 + 1), KEY);
  assert.equal(local._map.size, 0, 'nothing reached localStorage');
});

test('an expired key is dropped on read, so the next request finds none', () => {
  freshTab();
  setSessionKey(KEY, T0);
  assert.equal(sessionKey(T0 + SESSION_KEY_TTL_MS - 1), KEY);
  assert.equal(readSessionKey(T0 + SESSION_KEY_TTL_MS), null, 'the expiry instant is already past');
  assert.equal(tab.getItem(SESSION_KEY_ITEM), null, 'and the record is gone');
  assert.equal(sessionKey(T0), '', 'reading earlier again does not bring it back');
});

test('a reload of the same tab keeps it; a new tab starts without it', async () => {
  freshTab();
  setSessionKey(KEY, T0);
  const reloaded = await import('../../js/llm/sessionKey.js?reload');
  assert.equal(reloaded.sessionKey(T0 + 60_000), KEY, 'a fresh module over the same sessionStorage');
  freshTab();
  assert.equal(reloaded.sessionKey(T0 + 60_000), '');
  assert.equal(sessionKey(T0 + 60_000), '');
});

test('forget drops it now; an empty key forgets too; setting again restarts the clock', () => {
  freshTab();
  setSessionKey(KEY, T0);
  forgetSessionKey();
  assert.equal(readSessionKey(T0), null);
  setSessionKey(KEY, T0);
  assert.equal(setSessionKey('   ', T0), 0);
  assert.equal(tab.getItem(SESSION_KEY_ITEM), null);
  setSessionKey(KEY, T0);
  assert.equal(setSessionKey(KEY, T0 + 1000), T0 + 1000 + SESSION_KEY_TTL_MS);
});

test('a malformed record reads as no key and is removed', () => {
  for (const raw of ['{not json', 'null', '{"key":"","expiresAt":9e15}', '{"key":"k"}', '{"key":7,"expiresAt":9e15}']) {
    freshTab();
    tab.setItem(SESSION_KEY_ITEM, raw);
    assert.equal(readSessionKey(T0), null, raw);
    assert.equal(tab.getItem(SESSION_KEY_ITEM), null, raw);
  }
});

test('inert without a sessionStorage, and when the storage refuses', () => {
  useSession(undefined);
  assert.equal(setSessionKey(KEY, T0), 0);
  assert.equal(readSessionKey(T0), null);
  assert.doesNotThrow(() => forgetSessionKey());
  const refusing = { getItem() { throw new Error('SecurityError'); }, setItem() { throw new Error('SecurityError'); }, removeItem() { throw new Error('SecurityError'); } };
  useSession(refusing);
  assert.equal(setSessionKey(KEY, T0), 0);
  assert.equal(readSessionKey(T0), null);
  assert.doesNotThrow(() => forgetSessionKey());
  Object.defineProperty(globalThis, 'sessionStorage', { get() { throw new Error('SecurityError'); }, configurable: true });
  assert.equal(setSessionKey(KEY, T0), 0);
  assert.equal(sessionKey(T0), '');
  freshTab();
  assert.equal(local._map.size, 0);
});

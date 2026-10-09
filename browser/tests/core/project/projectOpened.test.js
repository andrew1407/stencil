// When this browser last opened each project (js/core/project/meta/projectOpened.js), behind
// the projects list's "Recently opened" orders.
import { test } from 'node:test';
import assert from 'node:assert/strict';

const store = new Map();
globalThis.localStorage = {
  getItem: (k) => (store.has(k) ? store.get(k) : null),
  setItem: (k, v) => store.set(k, String(v)),
  removeItem: (k) => store.delete(k),
};
const { PROJECT_OPENED_KEY, PROJECT_OPENED_CAP, projectOpenedAt, markProjectOpened } =
  await import('../../../js/core/project/meta/projectOpened.js');

test('an open is stamped, a project never opened reads 0, junk reads as nothing opened', () => {
  store.clear();
  assert.equal(projectOpenedAt('a'), 0);
  markProjectOpened('a', 1000);
  markProjectOpened('b', 2000);
  markProjectOpened('a', 3000);
  assert.deepEqual([projectOpenedAt('a'), projectOpenedAt('b'), projectOpenedAt('c')], [3000, 2000, 0]);
  store.set(PROJECT_OPENED_KEY, '{nope');
  assert.equal(projectOpenedAt('a'), 0);
  markProjectOpened(null);
  assert.equal(store.get(PROJECT_OPENED_KEY), '{nope', 'no id, no write');
});

test('only the newest entries are kept', () => {
  store.clear();
  for (let i = 1; i <= PROJECT_OPENED_CAP + 5; i++) markProjectOpened(`p${i}`, i);
  const table = JSON.parse(store.get(PROJECT_OPENED_KEY));
  assert.equal(Object.keys(table).length, PROJECT_OPENED_CAP);
  assert.equal(projectOpenedAt('p1'), 0);
  assert.equal(projectOpenedAt(`p${PROJECT_OPENED_CAP + 5}`), PROJECT_OPENED_CAP + 5);
});

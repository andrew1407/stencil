// Filtering the list (js/ui/motion.js): the excluded rows never play out, the rows left in
// take the light arrival, and a filtered-out row keeps its place in a pending selection.
import { test } from 'node:test';
import assert from 'node:assert';
import { FILTER_ENTERING_CLASS, FILTER_ENTER_MS } from '../js/ui/motion.js';
import { conn, openModal, rows, rowUrls, hasClass, find, sleep } from './helpers/connectModalRig.js';

// Both filter directions play the shared light filter effect, and neither is the
// disconnect's scatter: a filtered-out connection was not removed.

test('filtering rebuilds the list at once — the excluded rows never play out', () => {
  const { list, filter } = openModal([
    conn('http://adm:1', 'admin'), conn('http://plain:2', ''), conn('http://adm:3', 'admin'),
  ], { reduced: false });
  filter.value = 'admin';
  filter.dispatch('change');
  // The answer is on screen immediately: nothing about what you asked for waits on an
  // exit for a row that was never removed in the first place.
  assert.deepEqual(rowUrls(list), ['http://adm:1', 'http://adm:3'], 'the new answer, at once');
});

test('filtering plays the rows that are LEFT in, with the light arrival', () => {
  const { list, filter } = openModal([conn('http://adm:1', 'admin'), conn('http://plain:2', '')],
    { reduced: false });
  filter.value = 'admin';
  filter.dispatch('change');
  filter.value = 'all';
  filter.dispatch('change');
  assert.deepEqual(rowUrls(list), ['http://adm:1', 'http://plain:2']);
  const revealed = rows(list).find((r) => r.dataset.url === 'http://plain:2');
  assert.ok(hasClass(revealed, FILTER_ENTERING_CLASS), 'the revealed row arrives');
  assert.ok(!hasClass(revealed, 'materializing'), 'the dust gather stays for a real connect');
  const kept = rows(list).find((r) => r.dataset.url === 'http://adm:1');
  assert.ok(hasClass(kept, FILTER_ENTERING_CLASS),
    'and so does the one that was already listed — the filtered SET is what changed');
});

test('under reduced motion the filter still lands the right set, with no classes', () => {
  const { list, filter } = openModal([conn('http://adm:1', 'admin'), conn('http://plain:2', '')]);
  filter.value = 'admin';
  filter.dispatch('change');
  assert.deepEqual(rowUrls(list), ['http://adm:1'], 'straight to the re-render');
  assert.ok(!hasClass(rows(list)[0], FILTER_ENTERING_CLASS));
});

test('a row filtered out of view stays in a pending batch selection', async () => {
  const { list, filter } = openModal([conn('http://adm:1', 'admin'), conn('http://plain:2', '')],
    { reduced: false });
  const cb = find(rows(list).find((r) => r.dataset.url === 'http://plain:2'), 'connect-select');
  cb.checked = true;
  cb.dispatch('change');
  filter.value = 'admin';
  filter.dispatch('change');
  await sleep(FILTER_ENTER_MS + 80);
  assert.deepEqual(rowUrls(list), ['http://adm:1'], 'the non-admin row is out of view…');
  filter.value = 'all';
  filter.dispatch('change');
  const back = find(rows(list).find((r) => r.dataset.url === 'http://plain:2'), 'connect-select');
  assert.strictEqual(back.checked, true, '…but never dropped from the batch');
});

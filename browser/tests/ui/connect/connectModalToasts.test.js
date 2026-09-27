// What a connection change says (js/ui/connect/modal.js): a reconnect toast names the server,
// a disconnect posts none, and a refused or unreachable one plays its own arrival.
import { test } from 'node:test';
import assert from 'node:assert';
import { MATERIALIZE_CLASS, LEAVE_MS, BAR_HELD_CLASS, REVEAL_GROUP_OUT_MS } from '../../../js/ui/motion.js';
import { batchNote } from '../../../js/ui/connect/modal.js';
import { conn, openModal, rows, hasClass, find, sleep } from '../../helpers/connectModalRig.js';

// A reconnect toast must name WHICH server signed back in (user report); a disconnect posts
// none — the row scattering out is the notice, as on the desktop (ConnectDialog).

test('the row Reconnect toast names the server it signed back in', async () => {
  const url = 'http://localhost:8090';
  const done = [];
  const { list, notes } = openModal([conn(url, '')],
    { mgrExtra: { reconnectOne: async (u) => { done.push(u); } } });
  find(rows(list)[0], 'connect-reconnect-one').dispatch('click');
  await sleep(10);
  assert.deepEqual(done, [url], 'the real row button drove the manager');
  assert.deepEqual(notes.at(-1), [`Reconnected to ${url}`, 'ok']);
});

test('Reconnect all names the one server, and counts several', async () => {
  const one = openModal([conn('http://localhost:8090', '')], { mgrExtra: { reconnect: async () => {} } });
  one.doc.getElementById('connect-reconnect').dispatch('click');
  await sleep(10);
  assert.deepEqual(one.notes.at(-1), ['Reconnected to http://localhost:8090', 'ok']);

  const many = openModal([conn('http://a:1', ''), conn('http://b:2', '')],
    { mgrExtra: { reconnect: async () => {} } });
  many.doc.getElementById('connect-reconnect').dispatch('click');
  await sleep(10);
  assert.deepEqual(many.notes.at(-1), ['Reconnected 2 servers', 'ok']);

  assert.strictEqual(batchNote('Reconnected', ['http://x:1']), 'Reconnected to http://x:1');
  assert.strictEqual(batchNote('Reconnected', ['http://a:1', 'http://b:2']), 'Reconnected 2 servers');
});

test('disconnecting posts no toast — the row leaving the list is the notice', async () => {
  const url = 'http://localhost:8090';
  const gone = [];
  const { list, notes } = openModal([conn(url, '')], {
    mgrExtra: { disconnect: (u) => { gone.push(u); } },
    appExtra: { confirm: async () => true },
  });
  find(rows(list)[0], 'connect-disconnect').dispatch('click');
  await sleep(LEAVE_MS + 60);
  assert.deepEqual(gone, [url], 'the server really was forgotten…');
  assert.deepEqual(notes, [], '…and nothing was said about it');
});

// The selection bar is a REVEAL, not a display flip, which would cut Select all's own
// out-flight (user report); only with motion on do the two differ.
test('the batch bar opens and closes on the shared control flight', async () => {
  const known = ['http://a:1', 'http://b:2'];
  const modal = (urls) => {
    const m = openModal(urls.map((u) => conn(u, '')), { reduced: false,
      mgrExtra: { knownUrls: known, urls: known, disconnect: (u) => known.splice(known.indexOf(u), 1) },
      appExtra: { confirm: async () => true } });
    const bar = Object.assign(m.doc.getElementById('connect-batch-bar'), { getBoundingClientRect: () => ({ width: 300, height: 36 }) });
    return { ...m, bar, closing: () => hasClass(bar, BAR_HELD_CLASS) };
  };
  const { list, filter, closing } = modal(known);
  const pick = find(rows(list)[0], 'connect-select');
  pick.checked = true;
  pick.dispatch('change');
  filter.value = 'admin';
  filter.dispatch('change');
  assert.equal(rows(list).length, 0, 'every row is filtered out of view…');
  assert.ok(!closing(), '…and the pending selection still holds the bar');
  filter.value = 'all';
  filter.dispatch('change');
  find(rows(list)[0], 'connect-disconnect').dispatch('click');
  await sleep(0);
  assert.ok(!closing(), 'a row left standing keeps the bar');
  // The pool is the shown set MINUS the rows playing their removal dust, so the bar leaves
  // beside them instead of a flight later (desktop parity: connectDialog's `doomed_`).
  const last = modal(['http://b:2']);
  find(rows(last.list)[0], 'connect-disconnect').dispatch('click');
  await sleep(0);
  assert.ok(last.closing(), 'the bar sets off with the last row');
  assert.notEqual(last.bar.style.display, 'none', 'nothing flips the bar outright');
  await sleep(2 * REVEAL_GROUP_OUT_MS + 20);
  assert.equal(last.bar.style.display, 'none', 'it closes once its controls have flown');
});

// A refused credential keeps the server in `_expired`, so its row arrives out of the
// connections-changed echo and must fly in like the success path (user report).
test('a refused credential arrives as dust, like a successful one', async () => {
  const url = 'http://localhost:8090';
  const { doc, list, notes } = openModal([conn(url, '')], {
    reduced: false,
    mgrExtra: {
      connect: async () => { throw new Error('POST /auth/token: admin token required to issue tokens'); },
      isExpired: () => true,
    },
  });
  doc.getElementById('connect-url').value = url;
  doc.getElementById('connect-add').dispatch('click');
  await sleep(40);
  assert.match(notes.at(-1)?.[0] ?? '', /Could not connect/, 'it still says what went wrong');
  const row = rows(list).find((r) => r.dataset.url === url);
  assert.ok(row && hasClass(row, MATERIALIZE_CLASS), 'and the row it left behind gathers in');
  // The URL is in the list now, so the fields that put it there are done — leaving them
  // typed in invited adding the same server twice (user report).
  assert.equal(doc.getElementById('connect-url').value, '');
  assert.equal(doc.getElementById('connect-token').value, '');
});

// …but a server that is merely unreachable leaves NO row, so nothing is animated.
test('an unreachable server leaves no row and plays nothing', async () => {
  const { doc, list, notes } = openModal([], {
    reduced: false,
    mgrExtra: {
      connect: async () => { throw new Error('failed to fetch'); },
      isExpired: () => false,
    },
  });
  doc.getElementById('connect-url').value = 'http://nope:1';
  doc.getElementById('connect-add').dispatch('click');
  await sleep(40);
  assert.match(notes.at(-1)?.[0] ?? '', /Could not connect/);
  assert.equal(rows(list).length, 0);
  // …and nothing was added, so the text stays put to be corrected.
  assert.equal(doc.getElementById('connect-url').value, 'http://nope:1');
});

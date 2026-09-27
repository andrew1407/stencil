// The row's own dust (js/ui/connect/ + js/ui/motion.js): the expired-session prompt, the
// labelled dot, and a 44px row's finer grid, smaller throw and brisker clock — driven through
// the wired Servers modal (helpers/connectModalRig.js) with the motion on.
import { test } from 'node:test';
import assert from 'node:assert';
import {
  materialize, tileMotion, reshapeGrid, rowDustGrid, CONN_DUST_MS, LEAVE_MS, FILTER_DUST_MS,
  FILTER_DUST_DRIFT, REVEAL_GROUP_OUT_MS, DISINTEGRATE_MS,
} from '../../../js/ui/motion.js';
import { ANIMATIONS_CSS } from '../../helpers/css.js';
import { createStubElement } from '../../helpers/dom.js';
import { conn, openModal, rows, find, sleep } from '../../helpers/connectModalRig.js';

const ROW = { left: 0, top: 0, width: 440, height: 44 };
const sized = (el, r = ROW) => { el.getBoundingClientRect = () => r; return el; };
const rowOf = (list, url) => rows(list).find((r) => r.dataset.url === url);
const flush = async () => { for (let i = 0; i < 8; i++) await Promise.resolve(); };
// Every grain of a row cloud is tileMotion at `drift` on `ms`, over the grid `px` cuts the row into.
const isRowCloud = (host, { drift, ms, grid }) => {
  const { cols, rows: rs } = reshapeGrid(grid.cols, grid.rows, ROW.width, ROW.height, grid.px);
  return host.__cloud.motes.length === cols * rs && host.__cloud.motes.every((m, i) =>
    m.dx === tileMotion(i % cols, Math.floor(i / cols), cols, rs, !!host.className.includes('forming'), drift, ms).dx);
};

// An expired session's token prompt must not offer Reconnect with the box empty: the
// handler ignores an empty answer, so the button would close and do nothing (user report).
test('the expired-session prompt refuses an empty token', async () => {
  const url = 'http://localhost:8090';
  let asked = null;
  const { list } = openModal([conn(url, '')], {
    mgrExtra: {
      reconnectOne: async () => { const e = new Error('refused'); e.expired = true; throw e; },
    },
    appExtra: { prompt: async (_msg, opts) => { asked = opts; return null; } },
  });
  find(rows(list)[0], 'connect-reconnect-one').dispatch('click');
  await sleep(20);
  assert.ok(asked, 'the refusal raised the token prompt');
  assert.equal(asked.confirmLabel, 'Reconnect');
  assert.equal(typeof asked.validate, 'function', 'and the prompt is gated');
  assert.ok(asked.validate(''), 'an empty token is refused, with a reason');
  assert.equal(asked.validate('tok'), '', 'a pasted one is accepted');
});

// The status dot carries its own tooltip — what the dot means — inside a row label carrying
// the URL; both exist and differ, as on the desktop (serverAuth.headless.cpp).
test('the connection row labels the dot and the URL separately', () => {
  const { list } = openModal([conn('http://localhost:8090', '')]);
  const label = find(rows(list)[0], 'connect-url');
  assert.ok(label?.dataset.title, 'the row label says the status and the URL');
  assert.match(label.dataset.title, /http:\/\/localhost:8090/);
  const dotTitle = /class="conn-status[^"]*"\s+data-title="([^"]*)"/.exec(label.innerHTML)?.[1];
  assert.ok(dotTitle, 'and the dot inside it says what the dot means');
  assert.notEqual(dotTitle, label.dataset.title, '…which is not simply the row\'s own text');
});

// The row and the selection bar leave together: retire the row, disconnect and re-ask the
// bar in ONE turn, as ConnectDialog.cpp's rebuildList/updateBatchBar do (user report).
test('removing a connection retires the row and re-asks the bar in the same turn', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const [a, b] = ['http://a.test:1', 'http://b.test:2'];
  let atDisconnect = null;
  const { doc, list } = openModal([conn(a, ''), conn(b, '')], {
    reduced: false,
    mgrExtra: { disconnect: (u) => { atDisconnect = { leaving: rowOf(list, u).classList.contains('leaving') }; } },
    appExtra: { confirm: async () => true },
  });
  const count = sized(doc.getElementById('connect-batch-count'), { left: 0, top: 0, width: 80, height: 20 });
  const selectAll = doc.getElementById('connect-select-all');
  const cb = find(rowOf(list, a), 'connect-select');
  cb.checked = true;
  cb.dispatch('change');
  assert.equal(count.textContent, '1 selected');
  find(rowOf(list, a), 'connect-disconnect').dispatch('click');
  await flush();
  // The leave is STARTED, not awaited, before the disconnect and the bar update…
  assert.deepEqual(atDisconnect, { leaving: true });
  // …and a doomed row leaves the SELECTION too, in the same turn, not a whole flight later.
  assert.equal(count.textContent, '0 selected');
  // …on the app's own CONTROL clock, never the row's 220ms box collapse.
  assert.equal(count.__dustHost?.style['--dust-ms'], `${REVEAL_GROUP_OUT_MS}ms`);
  // A url stops counting as doomed only once the settle render has rebuilt the list without it.
  t.mock.timers.tick(LEAVE_MS);
  await flush();
  selectAll.dispatch('click');
  assert.equal(count.textContent, '1 selected', 'mid-scatter, Select all passes over the leaving row');
  t.mock.timers.tick(CONN_DUST_MS - 1);
  await flush();
  assert.equal(count.textContent, '1 selected');
  t.mock.timers.tick(1);
  await flush();
  selectAll.dispatch('click');
  assert.equal(count.textContent, '2 selected', 'released by the settle render, a wipe later');
});

// A connections row plays the same flight as a project row, but finer, tighter and brisker:
// 44px of row against a 74px project row makes the default grid a mosaic (user report).
test('the connections list dusts on its own finer grid, smaller throw and brisk clock', async () => {
  const [a, b] = ['http://a.test:1', 'http://b.test:2'];
  const known = [a];
  const mgrExtra = {
    knownUrls: known, urls: known, get: (u) => (known.includes(u) ? conn(u, '') : null), disconnect() {},
    connect: async (arg) => { known.push(arg.url ?? arg); }, isExpired: () => false,
  };
  const { doc, list } = openModal([], { reduced: false, mgrExtra, appExtra: { confirm: async () => true } });
  // A row gained is materialize's own recipe on the list-row grid and grain.
  const make = doc.createElement;
  doc.createElement = (tag) => (tag === 'div' ? sized(make(tag)) : make(tag));
  doc.getElementById('connect-url').value = b;
  doc.getElementById('connect-add').dispatch('click');
  await flush();
  const arrived = rowOf(list, b);
  assert.equal(arrived.__dustHost.style['--dust-ms'], `${FILTER_DUST_MS}ms`, 'the arrival keeps materialize\'s clock');
  assert.ok(isRowCloud(arrived.__dustHost, { drift: FILTER_DUST_DRIFT, ms: FILTER_DUST_MS, grid: rowDustGrid() }));
  // A removal adds the brisk clock and the smaller throw.
  sized(rowOf(list, a));
  find(rowOf(list, a), 'connect-disconnect').dispatch('click');
  await flush();
  const left = rowOf(list, a).__dustHost;
  assert.equal(left.style['--dust-ms'], `${CONN_DUST_MS}ms`);
  assert.ok(CONN_DUST_MS < DISINTEGRATE_MS);
  assert.ok(isRowCloud(left, { drift: 0.8, ms: CONN_DUST_MS, grid: rowDustGrid(1, 0) }), 'the row grid and its smaller throw');
});

// The arrival is the projects list's own filterDust recipe: the surface gather keyframes on
// half a throw and the filter's short clock (user report).
test('materialize gathers a row the way the projects list re-forms one', async () => {
  openModal([], { reduced: false });
  const host = createStubElement('div');
  const row = sized(createStubElement('div'));
  host.appendChild(row);
  materialize(row, rowDustGrid());
  const cloud = row.__dustHost;
  assert.equal(cloud.className, 'disintegrate-host dust-forming', 'the surface gather keyframes');
  assert.equal(cloud.parentNode, document.body, 'on <body>, not in the list');
  assert.equal(cloud.style['--dust-ms'], `${FILTER_DUST_MS}ms`, 'the filter\'s short clock');
  assert.ok(isRowCloud(cloud, { drift: FILTER_DUST_DRIFT, ms: FILTER_DUST_MS, grid: rowDustGrid() }), 'half a throw');
  // …and nothing brings a curve of its own any more: the row and the surface flights each
  // keep the one shared shape.
  assert.ok(!/--row-ease|--gather-ease/.test(ANIMATIONS_CSS));
  assert.ok(![...Object.keys(cloud.style), ...Object.keys(row.style)].some((k) => /ease/.test(k)));
});

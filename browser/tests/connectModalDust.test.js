// The row's own dust (js/ui/connectModal.js + js/ui/dustCloud.js): the expired-session prompt,
// the labelled dot, and a 44px row's finer grid, smaller throw and brisker clock.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { materialize } from '../js/ui/motion.js';
import { motionSource } from './helpers/motionSource.js';
import { ANIMATIONS_CSS } from './helpers/css.js';
import { conn, openModal, rows, find, sleep } from './helpers/connectModalRig.js';

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
test('removing a connection retires the row and re-asks the bar in the same turn', () => {
  const src = readFileSync(new URL('../js/ui/connect/connectModal.js', import.meta.url), 'utf8');
  const body = src.slice(src.indexOf('const confirmDisconnect'), src.indexOf('// Build the new url order'));
  // The leave is STARTED, not awaited, before the disconnect and the bar update…
  assert.match(body, /const leaving = leaveThenRemove\(/);
  assert.match(body, /doomed\.add\(url\);[\s\S]*const leaving =[\s\S]*mgr\(\)\.disconnect\(url\);[\s\S]*updateBatchBar\(\);[\s\S]*await leaving;/);
  // A doomed row leaves the SELECTION too, not just the shown pool, or "1 selected" and its
  // buttons outlive the row by a whole flight (user report).
  assert.match(body, /doomed\.add\(url\);\s*\n\s*selected\.delete\(url\);/);
  // …on the app's own CONTROL clock, never the row's 220ms box collapse: handed that, the
  // buttons' motes were a blink under the confirm dialog's close cloud (user report).
  assert.ok(!/updateBatchBar\(\{ ms/.test(body), 'no clock override');
  // A url stops counting as doomed only once the settle render has rebuilt the list without
  // it; an earlier release flashes Select all back on mid-scatter.
  assert.match(body, /await settle\(\);\s*\n(?:[^\n]*\n){0,3}\s*doomed\.delete\(url\);/);
});

// A connections row plays the same flight as a project row, but finer, tighter and brisker:
// 44px of row against a 74px project row makes the default grid a mosaic (user report).
test('the connections list dusts on its own finer grid, smaller throw and brisk clock', () => {
  const src = readFileSync(new URL('../js/ui/connect/connectModal.js', import.meta.url), 'utf8');
  // All four row flights share the list-row grid and grain (motion.js rowDustGrid); the
  // removals add the brisk clock and the smaller throw (rowLeaveDust).
  assert.equal((src.match(/rowLeaveDust\([^)]*CONN_DUST_MS\)/g) || []).length, 2);
  assert.match(src, /wait: \(\) => wipeDurationMs\(CONN_DUST_MS\)/, 'the hold waits out that clock');
  // …and the ARRIVALS keep materialize's own defaults — the projects list's filter recipe.
  assert.equal((src.match(/rowDustGrid\(\)/g) || []).length, 2);
  assert.equal((src.match(/materialize\(/g) || []).length, 2, 'and those are the two arrivals');
});

// The arrival is the projects list's own filterDust recipe: the surface gather keyframes on
// half a throw and the filter's short clock (user report).
test('materialize gathers a row the way the projects list re-forms one', () => {
  const src = motionSource();
  const body = src.slice(src.indexOf('export function materialize'), src.indexOf('// ── A chat entry ARRIVES'));
  assert.match(body, /dustMs = FILTER_DUST_MS,\s*\n\s*drift = FILTER_DUST_DRIFT/);
  assert.match(body, /gather: true, ms: dustMs, drift, toBody: true, hostClass: 'dust-forming',\s*\n\s*paintTile: speckPainter\(el\),/);
  assert.ok(!/reintegrate\(/.test(body), 'not the row gather');
  // …and nothing brings a curve of its own any more: the row and the surface flights each
  // keep the one shared shape.
  const css = ANIMATIONS_CSS;
  assert.ok(!/--row-ease|--gather-ease/.test(css));
  assert.ok(!/--row-ease|--gather-ease/.test(src));
});

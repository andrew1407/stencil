// The projects list's refresh gate and its batch bar: canRefreshList, the out-of-band triggers
// held while a wipe plays, and the rows' own scatter. From projectOpenGesture.test.js.
import { test } from 'node:test';
import assert from 'node:assert';

import { canRefreshList } from '../js/core/project/openGesture.js';
import { projectsModalSource } from './helpers/projectsModalSource.js';

// removing the ACTIVE project echoes straight back through onPeers and cuts the leave short.
test('canRefreshList: open + idle only — never mid-drag, never mid-removal', () => {
  assert.strictEqual(canRefreshList({ open: true }), true);
  assert.strictEqual(canRefreshList({ open: false }), false, 'a closed modal never re-renders');
  assert.strictEqual(canRefreshList({ open: true, dragging: true }), false, 'a rebuild would destroy the dragged row');
  assert.strictEqual(canRefreshList({ open: true, removing: true }), false, 'a rebuild would cut the leave short');
  assert.strictEqual(canRefreshList({ open: true, dragging: true, removing: true }), false);
  // The removal drained/the drag dropped → renders flow again.
  assert.strictEqual(canRefreshList({ open: true, dragging: false, removing: false }), true);
});

test('every out-of-band trigger routes through the shared gate, held while a wipe plays', () => {
  const src = projectsModalSource();
  // beginRemoval opens the hold; the settle renders at once, releasing it after the arrival.
  assert.match(src, /let removalsInFlight = 0;/, 'the hold is a counter (batch + single overlap)');
  assert.match(src, /removalsInFlight\+\+;\n\s+const held = list\.getBoundingClientRect\(\)\.height;/,
    'beginRemoval takes the hold with the height');
  assert.match(src, /await new Promise\(\(r\) => setTimeout\(r, Math\.max\(0, wipeDurationMs\(\) - ROW_ARRIVE_DELAY_MS\)\)\);\n\s+removalsInFlight = Math\.max\(0, removalsInFlight - 1\);/,
    'the hold outlives the wipe: released sooner, an out-of-band render replaced the arriving row mid-flight');
  // The four live triggers + the remote-listing settle ask mayRefresh (modal-open/dragActive/
  // removalsInFlight) rather than rendering outright; the fetch's arms share one gated `done`.
  const gated = (src.match(/if \(mayRefresh\(\)\) render\(\);/g) || []).length;
  assert.ok(gated >= 5, `connections-changed, projectsChanged, peers, incognito peers and the remote-listing settle all gate (found ${gated})`);
  assert.match(src, /remotes\.ensure\(\(\) => \{ if \(mayRefresh\(\)\) render\(\); \}\);/,
    'the remote-listing settle re-render routes through the same gate');
  assert.ok(!/if \(!dragActive && overlay\.classList\.contains\('modal-open'\)\) render\(\);/.test(src),
    'no trigger keeps the old drag-only guard (it let the peers echo render mid-wipe)');
});

// The selection bar is a REVEAL, not a display flip, twin of modal.js updateBatchBar: a
// flip cuts Select all's own out-flight. Pinned at the source — reduced motion hides it.
test('the projects batch bar opens and closes on the shared control flight', () => {
  const src = projectsModalSource();
  assert.match(src, /revealBar\(batchBar, \(\) => selected\.size > 0 \|\| anyLiveSelectable\(\)\)/);
  // The pool is the select-all set MINUS the rows playing their removal dust, so the bar
  // leaves beside them instead of a flight later (connections modal parity: `doomed`).
  assert.match(src, /const anyLiveSelectable = \(\) => \{[\s\S]*?!doomed\.has\(k\)\) return true;/);
  assert.ok(!/batchBar\.style\.display\s*=/.test(src), 'nothing flips the bar outright any more');
});

// The rows and the selection bar come apart TOGETHER: retire the rows and re-ask the bar in ONE
// turn, as the connections modal and the desktop dialog do (user report).
test('a batch removal retires the rows and re-asks the bar in the same turn', () => {
  const src = projectsModalSource();
  const body = src.slice(src.indexOf('batchBtns.remove.addEventListener'),
                         src.indexOf('batchBtns.moveServer.addEventListener'));
  // The leaves are STARTED, not awaited, before the selection is let go and the bar re-asked…
  assert.match(body, /for \(const k of keys\) doomed\.add\(k\);[\s\S]*const leaving = Promise\.all\([\s\S]*selected\.clear\(\);\s*\n\s*updateBatchBar\(\);\s*\n\s*await leaving;/);
  // …and the keys stop counting as doomed only after runBatch's SETTLE render: the rows
  // outlive their own box collapse, so an earlier release flashes Select all back on.
  assert.match(body, /'Removed', 'Could not remove', settle, rows\);\s*\n[^\n]*\n\s*for \(const k of keys\) doomed\.delete\(k\);/);
});

// Clear All is the same rule over the whole list: every selectable row is going, so the
// bar has nothing left to offer and must say so while the rows are still falling.
test('clear-all lets the selection bar go with the rows', () => {
  const src = projectsModalSource();
  const body = src.slice(src.indexOf('clearAllBtn.addEventListener'));
  assert.match(body, /const keys = \[\.\.\.selectables\.keys\(\)\];\s*\n\s*for \(const k of keys\) doomed\.add\(k\);[\s\S]*const leaving = Promise\.all\([\s\S]*updateBatchBar\(\);\s*\n\s*await leaving;/);
  assert.match(body, /await settle\(\);\s*\n\s*for \(const k of keys\) doomed\.delete\(k\);/);
});

// A single row removed from its own ⋯ menu counts too: it leaves the checked set and the
// select-all pool the moment its dust starts, not when the settle render arrives.
test('a single-row removal retires its key with the row', () => {
  const src = projectsModalSource();
  assert.match(src, /const retireKey = \(key\) => \{\s*\n\s*doomed\.add\(key\);\s*\n\s*selected\.delete\(key\);\s*\n\s*updateBatchBar\(\);/);
  // Every leaveThenRemove of ONE row is preceded by its retireKey, and the undo waits for
  // that path's settle render — never for the box collapse alone.
  assert.equal((src.match(/const revive = retireKey\(/g) || []).length, 4);
  assert.equal((src.match(/await settle\(\);\s*\n\s*revive\(\);/g) || []).length, 4);
});

// A project row comes apart in the same sand as a connection row — the finer grain and tighter
// throw (motion.js ROW_DUST_*) — on the projects list's own card clock (user report).
test('project rows scatter on the shared list-row grain and throw', () => {
  const src = projectsModalSource();
  const removals = (src.match(/leaveThenRemove\(/g) || []).length;
  assert.ok(removals >= 6, `every removal site (${removals})`);
  // …and each one takes the shared recipe on this list's card clock, nothing hand-rolled.
  assert.equal((src.match(/rowLeaveDust\([^)]*ITEM_DUST_MS\)/g) || []).length, removals);
});

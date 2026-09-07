import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

// Opening a project row: the gesture → intent mapping and the deferred-single-click
// machine behind it (browser/js/ui/projectsModal.js). Both are pure/injected, so the
// whole matrix — mouse and touch — is exercised without a DOM.
import {
  rowOpenIntent, createOpenGesture, DOUBLE_CLICK_MS, DRAG_SLOP_PX, canRefreshList,
} from '../js/ui/projectsModal.js';
import { isTouchLike, TOUCH_MEDIA } from '../js/utils.js';

// A controllable clock: timers fire only when the test advances it.
const stubTimers = () => {
  const jobs = new Map();
  let seq = 0;
  return {
    setTimer: (fn, ms) => { jobs.set(++seq, { fn, ms }); return seq; },
    clearTimer: (id) => jobs.delete(id),
    get pending() { return jobs.size; },
    // Run every timer whose delay is <= ms (the only delays here are the two constants).
    advance(ms) {
      for (const [id, job] of [...jobs]) {
        if (job.ms <= ms) { jobs.delete(id); job.fn(); }
      }
    },
  };
};
const recorder = () => {
  const intents = [];
  return { intents, run: (i) => intents.push(`${i.confirm ? 'confirm' : 'now'}:${i.target}`) };
};

// ── The mapping ──
test('rowOpenIntent: the full mouse + touch matrix', () => {
  // Mouse: a single click confirms; a double click acts immediately; ⌘/Ctrl targets a new tab.
  assert.deepStrictEqual(rowOpenIntent({ type: 'click' }), { confirm: true, target: 'here' });
  assert.deepStrictEqual(rowOpenIntent({ type: 'dblclick' }), { confirm: false, target: 'here' });
  assert.deepStrictEqual(rowOpenIntent({ type: 'click', metaKey: true }), { confirm: true, target: 'newtab' });
  assert.deepStrictEqual(rowOpenIntent({ type: 'click', ctrlKey: true }), { confirm: true, target: 'newtab' });
  assert.deepStrictEqual(rowOpenIntent({ type: 'dblclick', metaKey: true }), { confirm: false, target: 'newtab' });
  assert.deepStrictEqual(rowOpenIntent({ type: 'dblclick', ctrlKey: true }), { confirm: false, target: 'newtab' });
  // Keyboard activation behaves like the single click it replaces.
  assert.deepStrictEqual(rowOpenIntent({ type: 'key' }), { confirm: true, target: 'here' });
  assert.deepStrictEqual(rowOpenIntent({ type: 'key', ctrlKey: true }), { confirm: true, target: 'newtab' });
  // Touch: a tap confirms and opens here. There is deliberately NO hold gesture — the
  // list's press-and-hold picks the row up for reordering (touchDrag.js) — so nothing
  // maps to a long press at all; "open in a new tab" is the ⋯ menu's item there.
  assert.deepStrictEqual(rowOpenIntent({ type: 'tap' }), { confirm: true, target: 'here' });
  assert.deepStrictEqual(rowOpenIntent({ type: 'longpress' }), { confirm: true, target: 'here' },
    'no long-press mapping: it degrades to the safe default, it never targets a new tab');
  // Unknown/absent type degrades to the safest thing: confirm, this tab.
  assert.deepStrictEqual(rowOpenIntent(), { confirm: true, target: 'here' });
  assert.deepStrictEqual(rowOpenIntent({}), { confirm: true, target: 'here' });
});

// ── The crux: a double click must never flash the confirmation modal ──
test('a single click is deferred and CANCELLED by the double click that follows', () => {
  const timers = stubTimers();
  const rec = recorder();
  const g = createOpenGesture({ run: rec.run, touch: () => false, ...timers });

  g.click({});                     // first click of the pair — nothing runs yet
  assert.deepStrictEqual(rec.intents, [], 'no action before the double-click window closes');
  assert.ok(g.pendingClick);
  g.click({});                     // second click re-arms, still nothing
  assert.deepStrictEqual(rec.intents, []);
  g.dblclick({});                  // …and the dblclick cancels the pending single click
  assert.deepStrictEqual(rec.intents, ['now:here'], 'exactly one action: the immediate open');
  assert.strictEqual(g.pendingClick, false);
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(rec.intents, ['now:here'], 'the deferred click never fires afterwards');
  assert.strictEqual(timers.pending, 0, 'no timer left behind');
});

test('a lone click acts after the double-click interval', () => {
  const timers = stubTimers();
  const rec = recorder();
  const g = createOpenGesture({ run: rec.run, touch: () => false, ...timers });
  g.click({ metaKey: true });
  assert.deepStrictEqual(rec.intents, []);
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(rec.intents, ['confirm:newtab'], '⌘+click → confirmation, new tab');
  // ⌘+double click: immediate, new tab, and only once.
  g.click({ ctrlKey: true });
  g.dblclick({ ctrlKey: true });
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(rec.intents, ['confirm:newtab', 'now:newtab']);
});

test('cancel() drops a pending click (the rename editor uses it)', () => {
  const timers = stubTimers();
  const rec = recorder();
  const g = createOpenGesture({ run: rec.run, touch: () => false, ...timers });
  g.click({});
  g.cancel();
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(rec.intents, [], 'nothing opens behind the rename input');
  assert.strictEqual(g.pendingClick, false);
});

// ── Touch ──
test('touch: a tap acts at once (no double-click wait) and confirms in this tab', () => {
  const timers = stubTimers();
  const rec = recorder();
  const g = createOpenGesture({ run: rec.run, touch: () => true, ...timers });
  g.pressStart({ x: 10, y: 10 });
  assert.strictEqual(g.pressEnd(), false, 'a quick lift in place is a tap, not a drag');
  g.click({});
  assert.deepStrictEqual(rec.intents, ['confirm:here'], 'tap → confirmation, this tab');
  assert.strictEqual(timers.pending, 0, 'touch never arms the double-click timer');
  // A double click can't happen on touch; if one is synthesized it is ignored.
  g.dblclick({});
  assert.deepStrictEqual(rec.intents, ['confirm:here']);
});

test('touch: a hold is the list\'s REORDER pickup — it never opens anything', () => {
  const timers = stubTimers();
  const rec = recorder();
  const g = createOpenGesture({ run: rec.run, touch: () => true, ...timers });
  // Press and hold in place, well past any threshold, then release: the touch drag
  // engine owns this (it picks the row up at 280ms and swallows the trailing click).
  g.pressStart({ x: 40, y: 80 });
  g.dragStart();                                    // touchDrag.js onStart fires
  assert.strictEqual(g.pressEnd(), false);
  assert.deepStrictEqual(rec.intents, [], 'a hold never opens a project');
  // The click a drop may synthesize is swallowed…
  g.click({});
  assert.deepStrictEqual(rec.intents, []);
  // …and only that one: the next tap works normally.
  g.click({});
  assert.deepStrictEqual(rec.intents, ['confirm:here']);
});

test('MOVEMENT WINS: past the slop the gesture is a drag/scroll — the open is dropped', () => {
  const timers = stubTimers();
  const rec = recorder();
  const g = createOpenGesture({ run: rec.run, touch: () => true, ...timers });

  // A finger that travels (reorder drag, or a vertical scroll over the row).
  g.pressStart({ x: 40, y: 80 });
  assert.strictEqual(g.pressMove({ x: 40, y: 80 + DRAG_SLOP_PX }), false, 'within the slop: still a tap');
  assert.strictEqual(g.pressMove({ x: 40, y: 80 + DRAG_SLOP_PX + 1 }), true, 'past it: now a drag');
  assert.ok(g.dragging);
  assert.strictEqual(g.pressEnd(), true, 'the release reports a drag, not a tap');
  assert.deepStrictEqual(rec.intents, [], 'a completed drag opens nothing');
  g.click({});                                      // a drop-synthesized click…
  assert.deepStrictEqual(rec.intents, [], '…is swallowed too');

  // Horizontal travel (drag out to a zone) counts the same.
  g.pressStart({ x: 40, y: 80 });
  g.pressMove({ x: 40 + DRAG_SLOP_PX + 1, y: 80 });
  g.pressEnd();
  g.click({});
  assert.deepStrictEqual(rec.intents, []);
});

test('MOUSE: movement wins too — a deferred click dies when the drag starts', () => {
  const timers = stubTimers();
  const rec = recorder();
  const g = createOpenGesture({ run: rec.run, touch: () => false, ...timers });
  // Press, click (arms the 250ms deferral), then the HTML5 reorder drag begins.
  g.pressStart({ x: 10, y: 10 });
  g.click({});
  assert.ok(g.pendingClick);
  g.dragStart();
  assert.strictEqual(g.pendingClick, false, 'the pending open is cancelled by the pickup');
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(rec.intents, [], 'reordering never opens a project');
  // Same via pure travel, without a dragstart event.
  g.pressStart({ x: 10, y: 10 });
  g.click({});
  g.pressMove({ x: 10, y: 10 + DRAG_SLOP_PX + 1 });
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(rec.intents, []);
});

test('a slow, still press is just a click on either input', () => {
  for (const touch of [false, true]) {
    const timers = stubTimers();
    const rec = recorder();
    const g = createOpenGesture({ run: rec.run, touch: () => touch, ...timers });
    g.pressStart({ x: 0, y: 0 });
    assert.strictEqual(g.pressEnd(), false, 'no travel → not a drag');
    g.click({});
    timers.advance(DOUBLE_CLICK_MS);
    assert.deepStrictEqual(rec.intents, ['confirm:here'], `${touch ? 'touch' : 'mouse'}: a plain open`);
  }
});

// ── Wiring contract (what the DOM side must keep doing) ──
test('the row wires every gesture to the SAME open paths, and stays keyboard-usable', () => {
  const src = readFileSync(new URL('../js/ui/projectsModal.js', import.meta.url), 'utf8');
  // One intent runner, reusing the existing open paths — no duplicated open logic.
  assert.ok(src.includes('const openWithIntent = async ({ confirm = true, target = \'here\', closeAnchor = null } = {}) => {'));
  assert.ok(src.includes('app.openProjectInNewTab(meta.id);   // the same path the ⋯ menu uses'));
  assert.ok(src.includes('app.switchToProject(meta.id);'));
  assert.ok(src.includes('if (confirm && !(await confirmOpen(meta.name, true, closeAnchor))) return;'), 'new-tab wording');
  assert.ok(src.includes('const open = () => openWithIntent({ confirm: true, target: \'here\', closeAnchor: menuBtn });'), 'the ⋯ menu keeps its Open — flying back into the ⋯, not the menu row that is gone');
  // Every gesture goes through the machine.
  for (const wire of [
    "row.addEventListener('click', (e) => gesture.click(e));",
    "row.addEventListener('dblclick', (e) => gesture.dblclick(e));",
    "row.addEventListener('pointerdown', (e) => {",
    "row.addEventListener('pointermove', (e) => gesture.pressMove({ x: e.clientX, y: e.clientY }));",
    "row.addEventListener('dragstart', () => gesture.dragStart());",
  ]) assert.ok(src.includes(wire), `wired: ${wire}`);
  // Both drag engines cancel a pending open: HTML5 dragstart (mouse) and the touch
  // engine's pickup callback (touchDrag.js onStart).
  assert.ok(src.includes('row._openGesture?.dragStart(); dragKey = key;'), 'touch pickup cancels the open');
  // Reordering itself is untouched — the row is still draggable and still carries the
  // reorder flag the global drop overlay ignores.
  assert.ok(src.includes('row.draggable = true;'));
  assert.ok(src.includes("e.dataTransfer.setData('application/x-stencil-reorder', 'project')"));
  assert.ok(src.includes('makeTouchDraggable(row, {'), 'the touch reorder engine is still attached');
  // Keyboard: focusable, announced as a button, Enter/Space activate.
  assert.ok(src.includes('row.tabIndex = 0;') && src.includes("row.setAttribute('role', 'button');"));
  assert.ok(src.includes("if (e.key !== 'Enter' && e.key !== ' ') return;"));
  // The rename path cancels a pending open so no modal lands over the input.
  assert.ok(src.includes('rowGesture?.cancel();'));
  // The overflow menu still opens from a right-click / touch callout — that menu is
  // where "Open in new tab" lives for touch, since the hold belongs to reordering.
  assert.ok(src.includes("row.addEventListener('contextmenu', e => {\n          e.preventDefault();\n          showMenu(menuBtn, menuItems(), { x: e.clientX, y: e.clientY });"));
  assert.ok(src.includes("{ icon: 'external', label: 'Open in new tab', onClick:"), 'the touch route exists');
  // Touch detection is the app-wide helper, never a user-agent sniff. Matched on the
  // named import rather than the whole import line, which any unrelated helper added
  // to the same statement would otherwise break.
  assert.match(src, /import \{[^}]*\bisTouchLike\b[^}]*\} from '\.\.\/utils\.js';/);
  assert.ok(!/navigator\.userAgent/.test(src), 'no UA sniffing');
});

test('the touch rule is the app-wide media query, shared with the chat surfaces', () => {
  assert.ok(TOUCH_MEDIA.includes('(max-width: 680px)') && TOUCH_MEDIA.includes('(hover: none) and (pointer: coarse)'));
  assert.strictEqual(isTouchLike((q) => ({ matches: q === TOUCH_MEDIA })), true);
  assert.strictEqual(isTouchLike(() => ({ matches: false })), false);
  assert.strictEqual(isTouchLike(null), false, 'no matchMedia (Node) → desktop mapping');
  assert.strictEqual(isTouchLike(() => { throw new Error('bad query'); }), false);
  // The context menu's assistant gate is the very same rule (one helper, one behaviour).
  const ctx = readFileSync(new URL('../js/ui/contextMenu.js', import.meta.url), 'utf8');
  assert.ok(ctx.includes('const plain = isTouchLike();'));
});

test('constants + focus ring; the hold stays the reorder pickup, unstyled by us', () => {
  assert.strictEqual(DOUBLE_CLICK_MS, 250);
  assert.strictEqual(DRAG_SLOP_PX, 10);
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  assert.ok(css.includes('.project-row:focus-visible'), 'keyboard focus is visible');
  assert.strictEqual(css.split('.project-row.project-holding').length - 1, 0,
    'no press state of ours — the drag ghost is the hold feedback');
  // The list still scrolls by finger and the drag pickup keeps its own dim state.
  assert.ok(css.includes('touch-action: pan-y') && css.includes('.project-row.project-dragging'));
  // The touch reorder engine still owns the hold (its own threshold, unchanged).
  const drag = readFileSync(new URL('../js/ui/touchDrag.js', import.meta.url), 'utf8');
  assert.ok(drag.includes('longPressMs = 280'), 'reorder pickup threshold untouched');
});

// Hovering a project thumbnail shows the magnified image — and ONLY that. The metadata
// tooltip therefore lives on the TEXT column, never on the row: a native tooltip anywhere
// up the thumb's ancestor chain survives the move onto the thumb and covers the preview.
test('the metadata tooltip is on the text column, not the row', () => {
  const src = readFileSync(new URL('../js/ui/projectsModal.js', import.meta.url), 'utf8');
  assert.match(src, /if \(tip\) info\.dataset\.title = tip;/, 'the tooltip hangs off .project-info');
  assert.ok(!/if \(tip\) row\.dataset\.title = tip;/.test(src), 'never on the row — it would cover the preview');
});

// The stored thumbnail is only ~160 px wide, so a max-width can never enlarge it: the
// preview sets an explicit width, scaled from the thumbnail's own pixels.
test('the hover preview renders larger than the thumbnail', () => {
  const src = readFileSync(new URL('../js/ui/projectsModal.js', import.meta.url), 'utf8');
  assert.match(src, /const PREVIEW_ZOOM = 1\.67;/, 'the factor is named, not buried');
  assert.match(src, /const PREVIEW_MAX_VW = 0\.25;/, 'and so is the width ceiling');
  assert.match(src, /const PREVIEW_MAX_VH = 0\.20;/, 'and the height ceiling');
  // ONE scale factor for both axes. Leaving it to the CSS max-width/max-height pair
  // clamps each axis independently, which squashed a portrait thumbnail into a
  // letterboxed landscape box — lots of empty space beside a small image.
  assert.match(src, /const scale = Math\.min\(PREVIEW_ZOOM \* f,/, 'one scale factor, so the aspect ratio holds');
  assert.match(src, /img\.style\.width = `\$\{Math\.round\(zoomSize\.nw \* scale\)\}px`/, 'width from that factor');
  assert.match(src, /img\.style\.height = `\$\{Math\.round\(zoomSize\.nh \* scale\)\}px`/, 'height from the SAME factor');
  // Alt held doubles the glance — the factor rides both the zoom cap and the ceilings.
  assert.match(src, /const f = zoomAlt \? 2 : 1;/, 'the Alt factor is named');
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  // A hover preview is a GLANCE, not a lightbox. Once the stored thumbnail grew big
  // enough to magnify sharply, 90vw/80vh let it swallow the window — so it is capped
  // to a quarter of the width and a fifth of the height, with the list still readable
  // underneath. `width: auto` is what lets the HEIGHT cap bind on portrait images.
  const zoom = css.slice(css.indexOf('.project-thumb-zoom img {'));
  assert.match(zoom.slice(0, 260), /max-width: 25vw/, 'capped to a quarter of the viewport width');
  assert.match(zoom.slice(0, 260), /max-height: 20vh/, 'and a fifth of its height');
  assert.match(zoom.slice(0, 260), /width: auto/, 'so the height cap binds on tall thumbnails');
});

// ── Out-of-band refresh vs the removal wipe ──
// Removing the ACTIVE project reports the tab idle, and the worker's peers echo bounces
// straight back into onPeers — which re-rendered the list at ~LEAVE_MS, cutting the leave
// short and popping the empty temp-row placeholder in beneath the still-falling dust.
// canRefreshList is the shared gate: no rebuild mid-drag or while a removal is in flight.
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
  const src = readFileSync(new URL('../js/ui/projectsModal.js', import.meta.url), 'utf8');
  // beginRemoval opens the hold; the settle render releases it BEFORE re-rendering.
  assert.match(src, /let removalsInFlight = 0;/, 'the hold is a counter (batch + single overlap)');
  assert.match(src, /removalsInFlight\+\+;\n\s+const held = list\.getBoundingClientRect\(\)\.height;/,
    'beginRemoval takes the hold with the height');
  assert.match(src, /removalsInFlight = Math\.max\(0, removalsInFlight - 1\);\n\s+render\(\);/,
    'the settle render releases it first, so it is never blocked by its own hold');
  // The four live triggers + the remote-listing settle all ask mayRefresh (the gate
  // bound to modal-open/dragActive/removalsInFlight) instead of rendering outright.
  // The remote fetch's two arms now share ONE gated `done` (createRemoteListing).
  const gated = (src.match(/if \(mayRefresh\(\)\) render\(\);/g) || []).length;
  assert.ok(gated >= 5, `connections-changed, projectsChanged, peers, incognito peers and the remote-listing settle all gate (found ${gated})`);
  assert.match(src, /remotes\.ensure\(\(\) => \{ if \(mayRefresh\(\)\) render\(\); \}\);/,
    'the remote-listing settle re-render routes through the same gate');
  assert.ok(!/if \(!dragActive && overlay\.classList\.contains\('modal-open'\)\) render\(\);/.test(src),
    'no trigger keeps the old drag-only guard (it let the peers echo render mid-wipe)');
});

// The selection bar is a REVEAL, not a display flip — the connections modal's twin
// (connectModal.js updateBatchBar): flipping it took Select all's own out-flight off the
// screen before a frame of it showed. Pinned at the source, since under this suite's
// reduced motion revealControls lands on display:none too.
test('the projects batch bar opens and closes on the shared control flight', () => {
  const src = readFileSync(new URL('../js/ui/projectsModal.js', import.meta.url), 'utf8');
  assert.match(src, /revealBar\(batchBar, \(\) => selected\.size > 0 \|\| anyLiveSelectable\(\)\)/);
  // The pool is the select-all set MINUS the rows playing their removal dust, so the bar
  // leaves beside them instead of a flight later (connections modal parity: `doomed`).
  assert.match(src, /const anyLiveSelectable = \(\) => \{[\s\S]*?!doomed\.has\(k\)\) return true;/);
  assert.ok(!/batchBar\.style\.display\s*=/.test(src), 'nothing flips the bar outright any more');
});

// The rows and the selection bar must come apart TOGETHER. The batch removal used to hold
// the checked set until every delete had run, so the count, the batch buttons and Select
// all only started their own flight after the whole row scatter had finished — the items
// went, and the buttons went a beat later (user report). The connections modal and the
// desktop dialog both retire the rows and re-ask the bar in ONE turn.
test('a batch removal retires the rows and re-asks the bar in the same turn', () => {
  const src = readFileSync(new URL('../js/ui/projectsModal.js', import.meta.url), 'utf8');
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
  const src = readFileSync(new URL('../js/ui/projectsModal.js', import.meta.url), 'utf8');
  const body = src.slice(src.indexOf('clearAllBtn.addEventListener'));
  assert.match(body, /const keys = \[\.\.\.selectables\.keys\(\)\];\s*\n\s*for \(const k of keys\) doomed\.add\(k\);[\s\S]*const leaving = Promise\.all\([\s\S]*updateBatchBar\(\);\s*\n\s*await leaving;/);
  assert.match(body, /await settle\(\);\s*\n\s*for \(const k of keys\) doomed\.delete\(k\);/);
});

// A single row removed from its own ⋯ menu counts too: it leaves the checked set and the
// select-all pool the moment its dust starts, not when the settle render arrives.
test('a single-row removal retires its key with the row', () => {
  const src = readFileSync(new URL('../js/ui/projectsModal.js', import.meta.url), 'utf8');
  assert.match(src, /const retireKey = \(key\) => \{\s*\n\s*doomed\.add\(key\);\s*\n\s*selected\.delete\(key\);\s*\n\s*updateBatchBar\(\);/);
  // Every leaveThenRemove of ONE row is preceded by its retireKey, and the undo waits for
  // that path's settle render — never for the box collapse alone.
  assert.equal((src.match(/const revive = retireKey\(/g) || []).length, 4);
  assert.equal((src.match(/await settle\(\);\s*\n\s*revive\(\);/g) || []).length, 4);
});

// A project row comes apart in the SAME sand as a connection row: the finer grain and the
// tighter throw tuned on the connections list (motion.js ROW_DUST_*), on the projects
// list's own card clock — one look across both lists (user report: "same smoothness").
test('project rows scatter on the shared list-row grain and throw', () => {
  const src = readFileSync(new URL('../js/ui/projectsModal.js', import.meta.url), 'utf8');
  const removals = (src.match(/leaveThenRemove\(/g) || []).length;
  assert.ok(removals >= 6, `every removal site (${removals})`);
  // …and each one takes the shared recipe on this list's card clock, nothing hand-rolled.
  assert.equal((src.match(/rowLeaveDust\([^)]*ITEM_DUST_MS\)/g) || []).length, removals);
});

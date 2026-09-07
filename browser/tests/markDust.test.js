// ── A control's own mark, made of dust ──────────────────────────────────────
// A window is not the only thing in the app made of sand. A checkbox's tick, a select's
// chosen word, the rows of inputs a toggle reveals and the project rows a search filter
// moves are all MARKS: they come and go inside a control whose box never moves, and each
// forms out of motes and comes apart into them (js/ui/motion.js markIn / markOut /
// markSwap / revealControls / filterDust, wired by js/ui/controlSwap.js).
//
// What is pinned here: that a mark plays a ROW's fall rather than a surface's flight to a
// point (which is what the desktop indicator already does — the two surfaces must mime
// the same thing), that its throw and its grain are scaled down for a control, that the
// end state is written synchronously in BOTH directions so no layout waits on a
// decoration, and that the veil hides the mark alone and never the control around it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { tileMotion, MARK_IN_MS, MARK_OUT_MS, MARK_MOTE_PX, MARK_DRIFT, MARK_COLS, MARK_ROWS,
         MARK_FORMING_CLASS, MOTE_PX, SURFACE_COLS, SURFACE_ROWS, SURFACE_IN_MS,
         FILTER_DUST_MS, FILTER_DUST_DRIFT, FILTER_ENTER_MS, REVEAL_GROUP_IN_MS, REVEAL_GROUP_OUT_MS,
         reshapeGrid, markIn, markOut, markSwap, revealControls, settleMark, filterDust } from '../js/ui/motion.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const motionJs = read('../js/ui/motion.js');
const swapJs = read('../js/ui/controlSwap.js');
const animCss = read('../css/animations.css');
const selectJs = read('../js/ui/customSelect.js');

test('the throw scales with the control: a 15px tick cannot fling motes like a list row', () => {
  // Same cell, same hashes — only the distance differs, so the two are the SAME scatter.
  const row = tileMotion(3, 5, 20, 10);
  const mark = tileMotion(3, 5, 20, 10, false, MARK_DRIFT);
  assert.equal(mark.rot, row.rot, 'the spin is the control-independent part');
  assert.equal(mark.scale, row.scale);
  assert.equal(mark.delay, row.delay, 'and so is the sweep');
  assert.equal(mark.dx, Math.round(row.dx * MARK_DRIFT));
  assert.equal(mark.dy, Math.round(row.dy * MARK_DRIFT));
  assert.ok(MARK_DRIFT > 0 && MARK_DRIFT < 1, 'a mark throws a fraction of a row');
  // Undefaulted, a row is exactly what it always was.
  assert.deepEqual(tileMotion(3, 5, 20, 10, false, 1), row);
});

test('a mark is grained finer than a window, under a much smaller ceiling', () => {
  assert.ok(MARK_MOTE_PX < MOTE_PX, 'a 15px indicator needs a finer grain than a list row');
  assert.ok(MARK_COLS * MARK_ROWS < SURFACE_COLS * SURFACE_ROWS,
    'and a far smaller budget than a window: this is a 320ms decoration on a toolbar');
  // The indicator itself gets every mote the grain gives it…
  const tick = reshapeGrid(MARK_COLS, MARK_ROWS, 15, 15, MARK_MOTE_PX);
  assert.deepEqual(tick, { cols: 5, rows: 5 });
  // …while a 380px row of fields is thinned back rather than building thousands of nodes.
  const row = reshapeGrid(MARK_COLS, MARK_ROWS, 380, 28, MARK_MOTE_PX);
  const budget = MARK_COLS * MARK_ROWS;
  // The ceiling is an AIM — reshapeGrid scales both axes by one factor and rounds, so it
  // lands near the budget rather than exactly under it. What matters is the order of
  // magnitude: at the bare grain this row would have been over 1300 cells.
  assert.ok(row.cols * row.rows <= budget * 1.1, 'the ceiling holds');
  const bare = Math.round(380 / MARK_MOTE_PX) * Math.round(28 / MARK_MOTE_PX);
  assert.ok(bare > budget * 1.5 && row.cols * row.rows < bare * 0.7,
    '…and it really thinned the grid, rather than quietly passing it through');
  assert.ok(row.cols > tick.cols, 'a wider mark still gets more motes, just not unboundedly');
});

test('a mark FALLS like a row — it does not fly at a point like a window', () => {
  // The desktop scatters its checkbox indicator with Sweep::Fall / Sweep::Gather
  // (support/controlSwap.hpp swapCheckIndicator); the browser has to mime the same thing,
  // so markDust passes no `toward` and disintegrate takes the tileMotion branch.
  const body = motionJs.slice(motionJs.indexOf('const markDust ='),
                              motionJs.indexOf('export function settleMark'));
  assert.ok(!/toward/.test(body), 'a mark has no point to converge on — it falls');
  // (paint falls back to markPaint(el) — the app's muted ink, not the window's own
  // full-contrast text — so a mark never reads as a hard white/black fleck.)
  assert.match(body, /paintTile: speckPainter\(el, paint \|\| markPaint\(el\)\)/,
    'and it is specks, never clones');
  // …but on the SURFACE keyframes: a mark's motes ARE the mark, so they must be visible
  // from the first frame rather than spending their opening third near-transparent.
  assert.match(body, /hostClass: gather \? 'dust-forming' : 'dust-leaving'/);
  assert.ok(MARK_IN_MS > MARK_OUT_MS, 'arriving is the half you watch');
  assert.ok(MARK_IN_MS < SURFACE_IN_MS, 'and a mark is brisker than a whole window');
});

test('showing sets display at once; hiding collapses the slot before it goes', () => {
  const body = motionJs.slice(motionJs.indexOf('export function revealControls'),
                              motionJs.indexOf('export function markSwap'));
  // Shown: display FIRST — the group takes its slot immediately — THEN the gather
  // plays over the box it now occupies, growing that slot from zero in step.
  assert.match(body, /el\.style\.display = display;/);
  assert.match(body, /const played = markIn\(el, \{ ms: REVEAL_GROUP_IN_MS \}\);/);
  // Hidden: measured while still laid out (both the dust and the collapse start from
  // the true box), and a DECLINED flight (reduced motion, too small) still hides AT
  // ONCE — only a played one defers display:none until the slot has finished closing.
  assert.match(body, /const played = markOut\(el, \{ ms: REVEAL_GROUP_OUT_MS \}\);/);
  assert.match(body, /el\.style\.display = 'none';\s*\/\/ declined/);
  // And a group already in the asked-for state is not a flight at all.
  assert.match(body, /if \(wasShown === !!show\) return false;/);
  // markSwap writes the new value BETWEEN the two flights, so the element is never blank.
  const swap = motionJs.slice(motionJs.indexOf('export function markSwap'));
  assert.match(swap.slice(0, swap.indexOf('\n}')), /markOut\([\s\S]*?apply\(\);[\s\S]*?markIn\(/);
});

test('a group\'s own slot opens/closes in step with its dust, so a neighbour never jumps', () => {
  const body = motionJs.slice(motionJs.indexOf('export const REVEAL_GROUP_IN_MS'),
                              motionJs.indexOf('export function markSwap'));
  // A TRANSITION, not @keyframes: markIn/markOut may also put `.mark-forming` (an
  // animation) on this element, and two rules setting `animation` on one element
  // fight over a single winner — transition can never lose that fight.
  assert.match(body, /REVEAL_GROUP_TRANSITION_CLASS = 'reveal-group-transition'/);
  assert.match(body, /el\.classList\.add\(REVEAL_GROUP_TRANSITION_CLASS\)/g);
  // The size is committed as the transition's FROM value (a reflow between the two
  // writes), then the real value is set — never left for CSS to guess mid-flight.
  assert.match(body, /void el\.offsetWidth;/g);
  // …and the TO value waits for a PAINTED frame (double rAF): set in the same busy
  // turn, the opening frames stall while the transition's clock ticks, and the box
  // then leaps to wherever the curve already is — a sliver that jumps to full width.
  assert.match(body, /raf\(\(\) => raf\(go\)\);/);
  assert.match(body, /slideRevealSize\(el, sizeProp, '0px', `\$\{size\}px`, REVEAL_GROUP_IN_MS, \{ defer: true, slack: 40 \}\)/);
  assert.match(body, /slideRevealSize\(el, sizeProp, `\$\{size\}px`, '0px', REVEAL_GROUP_OUT_MS,/);
  // block (context-menu rows) collapses height; a horizontal toolbar row collapses
  // width. A caller may override: a full-width bar is a flex row that opens downward.
  assert.match(body, /const vertical = axis === null \? display === 'block' : !!axis;/);
  assert.match(body, /const sizeProp = vertical \? 'maxHeight' : 'maxWidth';/);
  // A group's slot is a wider move than a single mark and read as a snap on the mark's
  // own clock, so it has its own, longer one — and HANDS it to the dust, which is what
  // keeps the two landing together (the point the equality used to make).
  assert.ok(REVEAL_GROUP_IN_MS > MARK_IN_MS, 'a group opens slower than a single mark');
  assert.ok(REVEAL_GROUP_OUT_MS > MARK_OUT_MS, '…and closes slower too');
  assert.match(body, /markIn\(el, \{ ms: REVEAL_GROUP_IN_MS \}\)/, 'the dust rides the slot\'s clock');
  assert.match(body, /markOut\(el, \{ ms: REVEAL_GROUP_OUT_MS \}\)/, '…both ways');
});

test('animations.css: the slot collapse is a real transition, reduced motion off', () => {
  assert.match(animCss, /\.reveal-group-transition \{[^}]*transition: max-width var\(--reveal-ms/);
  assert.match(animCss, /max-height var\(--reveal-ms/, 'both axes ride the one class');
  assert.match(animCss,
    /@media \(prefers-reduced-motion: reduce\) \{\s*\.reveal-group-transition \{ transition: none; \}/);
});

test('an in-place swap plays two clouds over one element, and neither cancels the other', () => {
  // disintegrate() cancels whatever the element owns before it builds — right for a menu
  // opened twice, fatal for a value swap, whose outgoing cloud is still in the air when
  // the arrival starts. `own: false` is the opt-out.
  assert.match(motionJs, /const left = markOut\(el, \{ ms: outMs, paint, own: false \}\);/);
  assert.match(motionJs, /if \(own\) cancelDust\(el\);/);
  assert.match(motionJs, /if \(own\) \{ el\.__dustHost = host; el\.__dustTimer = life; \}/);
});

test('the veil hides the MARK, never the control around it', () => {
  assert.equal(MARK_FORMING_CLASS, 'mark-forming');
  const rule = animCss.slice(animCss.indexOf('.mark-forming {'));
  assert.match(rule, /@keyframes markForm \{ 0%, 6\d% \{ opacity: 0; \} 100% \{ opacity: 1; \} \}/,
    'the real mark waits until the motes have very nearly landed');
  // A checkbox keeps its outline throughout: only the accent fill and the tick are the
  // mark, so the veil suppresses those instead of the whole control's opacity.
  const box = rule.slice(rule.indexOf('input[type=checkbox].mark-forming'));
  const body = box.slice(0, box.indexOf('}'));
  assert.match(body, /animation: none;/, 'no opacity fade — that would blink the border');
  assert.match(body, /background-color: transparent;/);
  assert.match(body, /background-image: none;/);
  assert.match(animCss.slice(animCss.indexOf('.mark-forming {')),
    /@media \(prefers-reduced-motion: reduce\) \{\s*\n\s*\.mark-forming \{ animation: none; \}/);
});

test('one delegated listener wires every checkbox, as one filter does on the desktop', () => {
  assert.match(swapJs, /root\.addEventListener\('change'/, 'a click on the box or its label');
  assert.match(swapJs, /export function setChecked/, '…and a programmatic set animates too');
  assert.match(swapJs, /if \(el\.checked === on\) return;/,
    'a set to the state it already has is not a change to show');
  // The checked ink is READ, not guessed: the app-wide box fills with the accent and the
  // context menu's twin with the theme text colour. By the time an UNcheck is seen the
  // element paints nothing, so the last checked reading is cached.
  assert.match(swapJs, /if \(el\.checked\) \{ el\.__markInk = read\(\); return el\.__markInk; \}/);
  assert.match(swapJs, /if \(el\.__markInk\) return el\.__markInk;/);
  assert.match(swapJs, /if \(el\.type !== 'checkbox'\) return getComputedStyle\(el\)\.borderTopColor;/,
    'a radio cannot be poked for a reading — ticking one unticks its group');
  assert.match(swapJs, /if \(!ink\) return false;/,
    'an indicator that paints nothing has nothing to scatter (the f(x,y) pill)');
});

test('a select exchanges its chosen word, but only on a real change', () => {
  assert.match(selectJs, /markSwap\(cur, \(\) => \{ cur\.textContent = label; \}\)/);
  assert.match(selectJs, /if \(shown === null \|\| label === shown\) cur\.textContent = label;/,
    'the first paint and a re-sync on open write straight through');
});

test('a filter brings its rows in as sand — and never plays one out', () => {
  assert.ok(FILTER_DUST_MS <= FILTER_ENTER_MS * 2,
    'a view change has to keep up with typing in a search box');
  assert.ok(FILTER_DUST_DRIFT < 1, 'and it must never read as the delete it is not');
  const body = motionJs.slice(motionJs.indexOf('export const filterDust ='),
                              motionJs.indexOf('export const filterDelta'));
  assert.match(body, /scatterGridFor\(count, index\)/,
    'one shared mesh budget across every row the change moves');
  assert.match(body, /if \(!cols/, 'past the row ceiling a row simply fades, as it always did');
  assert.match(body, /paintTile: speckPainter\(el\)/, 'anonymous specks, never copies of a row');
  // One direction only: what a filter drops was never destroyed, so it does not come
  // apart — it is simply not the answer any more, and the answer is what arrives.
  assert.match(body, /gather: true/);
  assert.ok(!/gather: false|hostClass: 'dust-leaving'/.test(body), 'there is no exit to play');
  assert.match(motionJs, /filterDust\(el, i, rows\.length, boxes\[i\]\);/);
});

test('nothing here throws off-browser, or on a stub — decoration is never load-bearing', () => {
  for (const fn of [markIn, markOut, settleMark])
    assert.doesNotThrow(() => fn(null));
  assert.equal(markIn(null), false);
  assert.equal(markOut(undefined), false);
  assert.equal(markSwap({}, null), false, 'no writer, no swap');
  assert.equal(revealControls(null, true), false);
  assert.equal(filterDust(null, true), false);
  // A swap on a stub still WRITES the value — the DOM is never behind the state.
  let wrote = 0;
  markSwap({ getBoundingClientRect: () => ({ width: 0, height: 0 }) }, () => { wrote++; });
  assert.equal(wrote, 1);
});

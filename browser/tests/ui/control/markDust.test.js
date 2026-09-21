// A control's own mark made of dust: a tick, a chosen word, revealed rows, filtered project
// rows (js/ui/motion.js markIn / markOut / markSwap / revealControls / filterDust, wired by
// js/ui/swap.js). Pinned: a mark plays a ROW's fall as the desktop indicator does,
// scaled down for a control; the end state is written synchronously in both directions; and
// the veil hides the mark alone, never the control around it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import {
  tileMotion, MARK_IN_MS, MARK_OUT_MS, MARK_MOTE_PX, MARK_DRIFT, MARK_COLS, MARK_ROWS, MOTE_PX,
  SURFACE_COLS, SURFACE_ROWS, SURFACE_IN_MS, REVEAL_GROUP_IN_MS, REVEAL_GROUP_OUT_MS, reshapeGrid,
  markIn, markOut, markSwap, revealControls, settleMark, filterDust,
} from '../../../js/ui/motion.js';
import { motionSource } from '../../helpers/motionSource.js';
import { ANIMATIONS_CSS } from '../../helpers/css.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const motionJs = motionSource();
const swapJs = read('../../../js/ui/control/swap.js');
const animCss = ANIMATIONS_CSS;
const selectJs = read('../../../js/ui/control/customSelect.js');

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
  // The ceiling is an AIM: reshapeGrid scales both axes by one factor and rounds, so it lands
  // near the budget — the order of magnitude is the point (at the bare grain, 1300+ cells).
  assert.ok(row.cols * row.rows <= budget * 1.1, 'the ceiling holds');
  const bare = Math.round(380 / MARK_MOTE_PX) * Math.round(28 / MARK_MOTE_PX);
  assert.ok(bare > budget * 1.5 && row.cols * row.rows < bare * 0.7,
    '…and it really thinned the grid, rather than quietly passing it through');
  assert.ok(row.cols > tick.cols, 'a wider mark still gets more motes, just not unboundedly');
});

test('a mark FALLS like a row — it does not fly at a point like a window', () => {
  // The desktop scatters its checkbox indicator with Sweep::FALL / Sweep::GATHER
  // (support/controlSwap.hpp), so markDust passes no `toward` and takes the tileMotion branch.
  const body = motionJs.slice(motionJs.indexOf('const markDust ='),
                              motionJs.indexOf('export function settleMark'));
  assert.ok(!/toward/.test(body), 'a mark has no point to converge on — it falls');
  // (paint falls back to markPaint(el) — the app's muted ink, not the window's own
  // full-contrast text — so a mark never reads as a hard white/black fleck.)
  assert.match(body, /paintTile: painter \|\| speckPainter\(el, paint \|\| markPaint\(el\)\)/,
    'and it is specks, never clones');
  // …but on the SURFACE keyframes: a mark's motes ARE the mark, so they must be visible
  // from the first frame rather than spending their opening third near-transparent.
  assert.match(body, /hostClass: gather \? 'dust-forming' : 'dust-falling'/);
  assert.ok(MARK_IN_MS > MARK_OUT_MS, 'arriving is the half you watch');
  assert.ok(MARK_IN_MS < SURFACE_IN_MS, 'and a mark is brisker than a whole window');
});

test('showing sets display at once; hiding collapses the slot before it goes', () => {
  const body = motionJs.slice(motionJs.indexOf('export function revealControls'),
                              motionJs.indexOf('export function markSwap'));
  // Shown: display FIRST — the group takes its slot immediately — THEN the gather
  // plays over the box it now occupies, growing that slot from zero in step.
  assert.match(body, /el\.style\.display = display;/);
  assert.match(body, /const played = dust \? markIn\(el, \{ ms: inMs, painter: groupPainter\(el\) \}\) : !motionReduced\(\);/);
  // Hidden is measured while still laid out, so dust and collapse start from the true box; a
  // DECLINED flight hides at once, a played one defers display:none until the slot closes.
  assert.match(body, /const played = dust\s*\n\s*\? markOut\(el, \{ ms: outMs, painter: groupPainter\(el\), veil: MARK_LEAVING_CLASS \}\)\s*\n\s*: !motionReduced\(\);/);
  // `dust: false` still SLIDES the slot, skipping only the cloud (a full-width bar's motes are
  // a grey band); `inMs`/`outMs` are the group defaults unless the caller passes a clock.
  assert.match(body, /const inMs = ms \|\| REVEAL_GROUP_IN_MS;/);
  assert.match(body, /const outMs = ms \|\| REVEAL_GROUP_OUT_MS;/);
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
  // A TRANSITION, not @keyframes: markIn/markOut may also put `.mark-forming` (an animation) on
  // this element, and two rules setting `animation` fight over a single winner.
  assert.match(body, /REVEAL_GROUP_TRANSITION_CLASS = 'reveal-group-transition'/);
  assert.match(body, /el\.classList\.add\(REVEAL_GROUP_TRANSITION_CLASS\)/g);
  // The size is committed as the transition's FROM value (a reflow between the two
  // writes), then the real value is set — never left for CSS to guess mid-flight.
  assert.match(body, /void el\.offsetWidth;/g);
  // The TO value waits for a PAINTED frame (double rAF): set in the same busy turn, the box
  // leaps to wherever the transition's curve has already reached.
  assert.match(body, /raf\(\(\) => raf\(go\)\);/);
  assert.match(body, /slideRevealSize\(el, sizeProp, '0px', `\$\{size\}px`, inMs, \{ defer: true, slack: 40 \}\)/);
  assert.match(body, /slideRevealSize\(el, sizeProp, `\$\{size\}px`, '0px', outMs,/);
  // block (context-menu rows) collapses height; a horizontal toolbar row collapses
  // width. A caller may override: a full-width bar is a flex row that opens downward.
  assert.match(body, /const vertical = axis === null \? display === 'block' : !!axis;/);
  assert.match(body, /const sizeProp = vertical \? 'maxHeight' : 'maxWidth';/);
  // A group's slot is a wider move than a single mark, so it has its own longer clock and HANDS
  // it to the dust — that is what keeps the two landing together.
  assert.ok(REVEAL_GROUP_IN_MS > MARK_IN_MS, 'a group opens slower than a single mark');
  assert.ok(REVEAL_GROUP_OUT_MS > MARK_OUT_MS, '…and closes slower too');
  assert.match(body, /markIn\(el, \{ ms: inMs, painter: groupPainter\(el\) \}\)/, 'the dust rides the slot\'s clock');
  assert.match(body, /markOut\(el, \{ ms: outMs, painter: groupPainter\(el\), veil: MARK_LEAVING_CLASS \}\)/, '…both ways');
});

// The desktop's DisintegrateOverlay::setFollow: a control revealed beside a sibling is
// photographed where it sits, and the sibling's slot then pushes it along the row (user report).
test('a revealed group\'s cloud FOLLOWS the group while a sibling\'s slot moves it', () => {
  const body = motionJs.slice(motionJs.indexOf('const followDust ='),
                              motionJs.indexOf('export function markSwap'));
  // Re-anchored per painted frame, left/top only: the box the motes fly at is the natural
  // one, while the slot itself is mid-slide (max-width 0 → its width is not the cloud's).
  assert.match(body, /requestAnimationFrame\(step\)/);
  assert.match(body, /host\.style\.left = `\$\{r\.left\}px`;\s*host\.style\.top = `\$\{r\.top\}px`;/);
  assert.ok(!/style\.width|style\.height/.test(body), 'never resizes the cloud');
  // Stops with the host, at the end of the flight, or on display:none (an all-zero rect
  // would park the cloud at the page corner).
  assert.match(body, /if \(!host \|\| Date\.now\(\) - started >= ms\) return;/);
  assert.match(body, /if \(!r \|\| \(!r\.width && !r\.height\)\) return;/);
  // …and both directions of a played, dusted reveal ride it — the way out leaves beside
  // the count whose slot closes under it too.
  assert.match(body, /if \(dust && played\) followDust\(el, inMs\);/);
  assert.match(body, /if \(dust && played\) followDust\(el, outMs\);/);
});

test('animations.css: the slot collapse is a real transition, reduced motion off', () => {
  assert.match(animCss, /\.reveal-group-transition \{[^}]*transition: max-width var\(--reveal-ms/);
  assert.match(animCss, /max-height var\(--reveal-ms/, 'both axes ride the one class');
  assert.match(animCss,
    /@media \(prefers-reduced-motion: reduce\) \{\s*\.reveal-group-transition \{ transition: none; \}/);
});

test('an in-place swap plays two clouds over one element, and neither cancels the other', () => {
  // disintegrate() cancels whatever the element owns before it builds — right for a menu opened
  // twice, fatal for a value swap still in the air; `own: false` is the opt-out.
  assert.match(motionJs, /const left = markOut\(el, \{ ms: outMs, paint, own: false \}\);/);
  assert.match(motionJs, /if \(own\) cancelDust\(el\);/);
  assert.match(motionJs, /if \(own\) \{ el\.__dustHost = host; el\.__dustTimer = life; \}/);
});

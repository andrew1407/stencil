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
         reshapeGrid, markIn, markOut, markSwap, revealControls, revealBar, settleMark, filterDust,
         BAR_HELD_CLASS, BAR_CLOSING_CLASS } from '../js/ui/motion.js';

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
  // Hidden: measured while still laid out (both the dust and the collapse start from
  // the true box), and a DECLINED flight (reduced motion, too small) still hides AT
  // ONCE — only a played one defers display:none until the slot has finished closing.
  assert.match(body, /const played = dust\s*\n\s*\? markOut\(el, \{ ms: outMs, painter: groupPainter\(el\), veil: MARK_LEAVING_CLASS \}\)\s*\n\s*: !motionReduced\(\);/);
  // `dust: false` still SLIDES the slot — it only skips the cloud (a full-width bar's
  // motes are a grey band, not a motion the eye can follow), and reduced motion still
  // declines the whole thing. `inMs`/`outMs` are the group defaults unless the caller
  // hands the slot a clock of its own (a removal, so the strip lands with its row).
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
  assert.match(body, /slideRevealSize\(el, sizeProp, '0px', `\$\{size\}px`, inMs, \{ defer: true, slack: 40 \}\)/);
  assert.match(body, /slideRevealSize\(el, sizeProp, `\$\{size\}px`, '0px', outMs,/);
  // block (context-menu rows) collapses height; a horizontal toolbar row collapses
  // width. A caller may override: a full-width bar is a flex row that opens downward.
  assert.match(body, /const vertical = axis === null \? display === 'block' : !!axis;/);
  assert.match(body, /const sizeProp = vertical \? 'maxHeight' : 'maxWidth';/);
  // A group's slot is a wider move than a single mark and read as a snap on the mark's
  // own clock, so it has its own, longer one — and HANDS it to the dust, which is what
  // keeps the two landing together (the point the equality used to make).
  assert.ok(REVEAL_GROUP_IN_MS > MARK_IN_MS, 'a group opens slower than a single mark');
  assert.ok(REVEAL_GROUP_OUT_MS > MARK_OUT_MS, '…and closes slower too');
  assert.match(body, /markIn\(el, \{ ms: inMs, painter: groupPainter\(el\) \}\)/, 'the dust rides the slot\'s clock');
  assert.match(body, /markOut\(el, \{ ms: outMs, painter: groupPainter\(el\), veil: MARK_LEAVING_CLASS \}\)/, '…both ways');
});

// The desktop's DisintegrateOverlay::setFollow: a control revealed beside a sibling is
// photographed where it sits at that instant, and the sibling's slot then pushes it along
// the row. The selection bar's count opens ahead of the action group, so without this the
// real buttons slid right while their motes gathered where the group first stood — formed
// to the left, then jumped over on landing (user report, with pictures).
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
  assert.match(selectJs, /const changed = shown !== null && label !== shown;\s*\n\s*if \(!changed\) cur\.textContent = label;/,
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

// ── The selection bar: a revealed group holding revealed controls ───────────
// Its own slot CLIPS them (overflow:hidden while the max-height collapses), so closing it
// in the same turn as the buttons inside took their dust and their slide off the screen
// before a frame of either showed — they blinked out with no animation at all (user
// report, projects modal). Opening still has to be immediate: the controls need the slot
// before they can fly into it. The desktop has always deferred the hide the same way
// (ProjectsDialog / ConnectDialog::updateBatchBar).
test('revealBar defers the CLOSE by its contents\' flight, but opens at once', () => {
  const jobs = [];
  const setTimer = (fn, ms) => { jobs.push({ fn, ms }); return jobs.length; };
  // A stub, not a DOM node: revealControls measures the slot it is collapsing.
  const bar = { style: { display: 'flex' }, getBoundingClientRect: () => ({ width: 0, height: 0 }) };

  // Closing: nothing happens this turn…
  assert.equal(revealBar(bar, () => false, { setTimer }), false);
  assert.equal(bar.style.display, 'flex', 'the strip is still there while the buttons fly');
  assert.equal(jobs.length, 1);
  assert.equal(jobs[0].ms, REVEAL_GROUP_OUT_MS, 'the group out-flight is the default wait');
  jobs[0].fn();
  assert.equal(bar.style.display, 'none', '…and only then does the slot go');

  // …and a removal's own clock is honoured, so the strip lands with its rows.
  bar.style.display = 'flex';
  jobs.length = 0;
  revealBar(bar, () => false, { ms: 220, setTimer });
  assert.equal(jobs[0].ms, 220);

  // Re-asked on arrival: a selection made while the flight played keeps the bar.
  bar.style.display = 'flex';
  jobs.length = 0;
  let wanted = false;
  revealBar(bar, () => wanted, { setTimer });
  wanted = true;
  jobs[0].fn();
  assert.equal(bar.style.display, 'flex', 'want() said yes in the meantime');

  // Opening never waits — the controls need the slot before they can fly into it.
  bar.style.display = 'none';
  jobs.length = 0;
  revealBar(bar, () => true, { setTimer });
  assert.equal(jobs.length, 0, 'no timer at all on the way in');
  assert.equal(bar.style.display, 'flex');
  // …and an already-closed bar asked to close again settles now, not a flight later.
  jobs.length = 0;
  bar.style.display = 'none';
  revealBar(bar, () => false, { setTimer });
  assert.equal(jobs.length, 0);
  // Decoration is never load-bearing.
  assert.doesNotThrow(() => revealBar(null, () => false, { setTimer }));
  // …and the bar's own box is never animated, either way: sliding its max-height clipped
  // Select all to the height reached so far on the way in, and left the border and
  // padding standing as a bare grey line on the way out (user report, with pictures).
  // Only the controls fly — the desktop's updateBatchBar exactly.
  assert.equal(bar.style.maxHeight, undefined);
  const src = readFileSync(new URL('../js/ui/motion.js', import.meta.url), 'utf8');
  const body = src.slice(src.indexOf('export const revealBar'), src.indexOf('const REVEAL_GROUP_TRANSITION_CLASS'));
  assert.ok(!/revealControls|slideRevealSize|maxHeight/.test(body), 'a display flip, nothing more');
});

// …and the strip does not simply vanish when that wait is over: the row under it would be
// dropped upward in one frame (user report — the pinned "Temporary (unsaved)" row jumped
// when Select all left). Its footprint is HELD from the moment it is asked to leave (the
// controls flying out inside it must not collapse it first), and then height, padding and
// the divider close together. Desktop twin: controlReveal holdBarSlot / closeBarSlot.
test('a leaving bar HOLDS its slot, then closes the whole footprint', () => {
  const jobs = [];
  const setTimer = (fn, ms) => { jobs.push({ fn, ms }); return jobs.length; };
  const classes = new Set();
  const props = new Map();
  const bar = {
    style: {
      display: 'flex',
      setProperty: (k, v) => props.set(k, v),
      removeProperty: (k) => props.delete(k),
      getPropertyValue: (k) => props.get(k) || '',
    },
    classList: {
      add: (...c) => c.forEach((x) => classes.add(x)),
      remove: (...c) => c.forEach((x) => classes.delete(x)),
    },
    getBoundingClientRect: () => ({ width: 300, height: 47 }),
  };

  revealBar(bar, () => false, { setTimer });
  assert.ok(classes.has(BAR_HELD_CLASS), 'held the instant it is asked to leave');
  assert.equal(props.get('--bar-h'), '47px', '…at the footprint it actually has');
  assert.ok(!classes.has(BAR_CLOSING_CLASS), 'but nothing closes while the controls fly');
  assert.equal(bar.style.display, 'flex');

  jobs[0].fn();
  assert.ok(classes.has(BAR_CLOSING_CLASS), 'and then the slot closes');
  assert.equal(bar.style.display, 'flex', 'still in the flow while it does — that IS the slide');
  jobs[1].fn();
  assert.equal(bar.style.display, 'none', 'out of the flow only once it has closed');
  assert.equal(classes.size, 0, 'and nothing of the flight is left on it');
  assert.equal(props.size, 0);

  // Asked back mid-close: the hold comes straight off, or the bar stays squeezed.
  bar.style.display = 'flex';
  jobs.length = 0;
  revealBar(bar, () => false, { setTimer });
  assert.ok(classes.has(BAR_HELD_CLASS));
  revealBar(bar, () => true, { setTimer });
  assert.equal(classes.size, 0, 'the freeze is released when it is wanted again');
  assert.equal(bar.style.display, 'flex');
});

// The CSS half of that close: whatever the bar's footprint is made of has to go with the
// height, or the list below still drops by the leftovers in the frame it is hidden.
test('the closing bar zeroes its padding and divider, not just its height', () => {
  const css = readFileSync(new URL('../css/animations.css', import.meta.url), 'utf8');
  const rule = css.match(/\.bar-held\.bar-closing \{[^}]*\}/)?.[0] || '';
  for (const prop of ['height: 0', 'max-height: 0', 'padding-top: 0', 'padding-bottom: 0',
                      'border-bottom-width: 0']) {
    assert.ok(rule.includes(prop), `the close takes ${prop} with it`);
    assert.match(rule, new RegExp(`transition:[^;]*${prop.split(':')[0]}`, 's'),
      `${prop.split(':')[0]} is animated, not dropped`);
  }
  assert.match(css, /\.bar-held \{[^}]*overflow: hidden/, 'the held slot clips its contents');
});

// A cloud is parented to <body> so it clears the scroller it was started in — which means
// it OUTLIVES the window that started it: removing a project and closing the window left
// its motes coming apart mid-air over the page (user report). Every window sweeps its own
// on the way out; its own close flight starts after that, so it is never caught.
test('sweepDust takes down the clouds a window started, and only those', async () => {
  const { sweepDust } = await import('../js/ui/motion.js');
  const made = [];
  const host = (scope) => {
    const stopped = { stopped: false };
    const el = {
      className: 'disintegrate-host',
      dataset: scope ? { dustScope: scope } : {},
      __stop: () => { stopped.stopped = true; },
      remove: () => { el.removed = true; },
      removed: false,
      stopped,
    };
    made.push(el);
    return el;
  };
  const mine = host('projects-modal-overlay');
  const alsoMine = host('projects-modal-overlay');
  const anothers = host('connect-modal-overlay');
  const loose = host(null);
  const realDoc = globalThis.document;
  globalThis.document = { querySelectorAll: () => made };
  try {
    assert.strictEqual(sweepDust('projects-modal-overlay'), 2, 'both of that window\'s clouds');
    assert.ok(mine.removed && mine.stopped.stopped, 'the layer goes, and its loop stops first');
    assert.ok(alsoMine.removed);
    assert.ok(!anothers.removed, 'another window\'s cloud is left alone');
    assert.ok(!loose.removed, '…and so is one that belongs to no window');
    assert.strictEqual(sweepDust(null), 0, 'no scope, nothing swept');
    assert.strictEqual(sweepDust({ id: 'connect-modal-overlay' }), 1, 'an element works too');
  } finally {
    globalThis.document = realDoc;
  }
});

// …and the shell is what calls it: every window closes the same way (base.js close()).
test('every modal shell sweeps its own dust as it closes', () => {
  const base = read('../js/ui/base.js');
  assert.match(base, /const close = \(\) => \{[\s\S]{0,400}sweepDust\(overlay\);/,
    'the shared close sweeps, so no window has to remember to');
  assert.match(base, /import \{[^}]*sweepDust[^}]*\} from '\.\/motion\.js'/);
});

// A control's own mark made of dust: a tick, a chosen word, revealed rows, filtered project
// rows (js/ui/motion.js markIn / markOut / markSwap / revealControls / filterDust, wired by
// js/ui/control/swap.js). Pinned: a mark plays a ROW's fall as the desktop indicator does,
// scaled down for a control; the end state is written synchronously in both directions; and
// the veil hides the mark alone, never the control around it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDustDom, rect, boxEl, cloudKind } from '../../helpers/dustCloudRig.js';
import { ANIMATIONS_CSS } from '../../helpers/css.js';

const dust = installDustDom();
const {
  tileMotion, MARK_IN_MS, MARK_OUT_MS, MARK_MOTE_PX, MARK_DRIFT, MARK_COLS, MARK_ROWS, MOTE_PX,
  SURFACE_COLS, SURFACE_ROWS, SURFACE_IN_MS, REVEAL_GROUP_IN_MS, REVEAL_GROUP_OUT_MS, reshapeGrid,
  markIn, markOut, markSwap, revealControls, disintegrate, MARK_LEAVING_CLASS,
} = await import('../../../js/ui/motion.js');
const { setMotionPrefs } = await import('../../../js/ui/motion/motionPrefs.js');
const animCss = ANIMATIONS_CSS;

// A group whose box is its natural size while displayed, nothing while display:none; every
// read of offsetWidth (the reflow) notes the size property it committed.
const group = (w = 120, h = 24) => {
  const el = boxEl(() => (el.style.display === 'none' ? rect(0, 0, 0, 0) : rect(40, 10, w, h)));
  el.style.display = 'none';
  el.reflows = [];
  Object.defineProperty(el, 'offsetWidth', { get: () => { el.reflows.push(el.style.maxWidth ?? el.style.maxHeight); return w; } });
  return el;
};

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
  // (support/control/swap/controlSwap.hpp): every grain is a row's tileMotion at control scale, no point.
  const el = boxEl(rect(10, 10, 15, 15));
  assert.equal(markOut(el), true);
  const { cols, rows } = reshapeGrid(MARK_COLS, MARK_ROWS, 15, 15, MARK_MOTE_PX);
  const out = el.__dustHost.__cloud;
  assert.equal(out.flight, 'fall');
  out.motes.forEach((m, i) => {
    const t = tileMotion(i % cols, Math.floor(i / cols), cols, rows, false, MARK_DRIFT, MARK_OUT_MS);
    assert.deepEqual([m.dx, m.dy], [t.dx, t.dy], 'a mark has no point to converge on — it falls');
  });
  // …but on the SURFACE keyframes: its motes ARE the mark, visible from the first frame.
  assert.equal(cloudKind(el), 'dust-falling');
  markIn(el);
  assert.equal(cloudKind(el), 'dust-forming');
  assert.equal(el.__dustHost.__cloud.flight, 'surfaceGather');
  // Specks, never clones; a caller's painter wins over the muted mark ink.
  assert.equal(el.__dustHost.children.length, 0);
  assert.ok(el.__dustHost.__cloud.motes.every((m) => m.a >= 0.78 && m.a <= 1));
  markIn(el, { painter: () => ({ px: 2, alpha: 0.5 }) });
  assert.ok(el.__dustHost.__cloud.motes.every((m) => m.a === 0.5 && m.r === 1));
  assert.ok(MARK_IN_MS > MARK_OUT_MS, 'arriving is the half you watch');
  assert.ok(MARK_IN_MS < SURFACE_IN_MS, 'and a mark is brisker than a whole window');
});

test('showing sets display at once; hiding collapses the slot before it goes', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  setMotionPrefs({ mode: 'particles' });
  // Shown: display FIRST, then the gather plays over the box it now occupies.
  const el = group();
  assert.equal(revealControls(el, true), true);
  assert.equal(el.style.display, 'inline-flex');
  assert.equal(cloudKind(el), 'dust-forming');
  assert.equal(el.__dustHost.style.width, '120px', 'measured at its natural size');
  assert.equal(el.__dustHost.style['--dust-ms'], `${REVEAL_GROUP_IN_MS}ms`, 'the group clock');
  assert.equal(revealControls(el, true), false, 'a group already shown is not a flight at all');
  // Hidden: measured while still laid out; a played flight defers display:none to the slot.
  assert.equal(revealControls(el, false), true);
  assert.equal(cloudKind(el), 'dust-falling');
  assert.equal(el.__dustHost.style['--dust-ms'], `${REVEAL_GROUP_OUT_MS}ms`);
  assert.ok(el.classes.has(MARK_LEAVING_CLASS), 'the real group goes behind a veil');
  assert.equal(el.style.display, 'inline-flex');
  t.mock.timers.tick(REVEAL_GROUP_OUT_MS);
  assert.equal(el.style.display, 'none', '…until the slot closes');
  // A caller's clock is both the slot's and the dust's.
  const timed = group();
  revealControls(timed, true, 'flex', { ms: 222 });
  assert.equal(timed.__dustHost.style['--dust-ms'], '222ms');
  // `dust: false` still slides the slot, skipping only the cloud.
  const plain = group();
  revealControls(plain, true, 'flex', { dust: false });
  assert.equal(plain.__dustHost ?? null, null);
  assert.ok(plain.classes.has('reveal-group-transition'));
  // A DECLINED flight hides at once.
  setMotionPrefs({ mode: 'none' });
  assert.equal(revealControls(timed, false), false);
  assert.equal(timed.style.display, 'none', 'declined: hidden this very call');
  setMotionPrefs({ mode: 'particles' });
  // markSwap writes the new value BETWEEN the two flights, so the element is never blank.
  dust.reset();
  const word = boxEl(rect(0, 0, 60, 18));
  let seen = null;
  markSwap(word, () => { seen = { falling: dust.clouds().some((h) => h.className.endsWith('dust-falling')), forming: !!word.__dustHost }; });
  assert.deepEqual(seen, { falling: true, forming: false });
  assert.equal(cloudKind(word), 'dust-forming');
});

test('a group\'s own slot opens/closes in step with its dust, so a neighbour never jumps', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  // A TRANSITION, not @keyframes: markIn/markOut may also put `.mark-forming` on it.
  const el = group();
  revealControls(el, true);
  assert.ok(el.classes.has('reveal-group-transition'));
  // The FROM size is committed by a reflow; the TO waits for a PAINTED frame (double rAF).
  assert.deepEqual(el.reflows, ['0px']);
  assert.equal(el.style.maxWidth, '0px');
  dust.frame();
  assert.equal(el.style.maxWidth, '0px', 'not in the same busy turn');
  dust.frame();
  assert.equal(el.style.maxWidth, '120px');
  assert.equal(el.style['--reveal-ms'], `${REVEAL_GROUP_IN_MS}ms`, 'the dust rides the slot\'s clock');
  t.mock.timers.tick(REVEAL_GROUP_IN_MS + 40);
  assert.ok(!el.classes.has('reveal-group-transition') && el.style.maxWidth === '');
  revealControls(el, false);
  assert.deepEqual(el.reflows.slice(-1), ['120px'], 'the close starts from the true size');
  assert.equal(el.style.maxWidth, '0px', 'and closes at once');
  assert.equal(el.style['--reveal-ms'], `${REVEAL_GROUP_OUT_MS}ms`);
  // block (context-menu rows) collapses height; a caller may force the axis.
  for (const [display, opts, prop] of [['block', {}, 'maxHeight'], ['flex', { vertical: true }, 'maxHeight'], ['flex', {}, 'maxWidth']]) {
    const g = group();
    revealControls(g, true, display, opts);
    assert.equal(g.style[prop], '0px', `${display} ${JSON.stringify(opts)} slides ${prop}`);
  }
  assert.ok(REVEAL_GROUP_IN_MS > MARK_IN_MS, 'a group opens slower than a single mark');
  assert.ok(REVEAL_GROUP_OUT_MS > MARK_OUT_MS, '…and closes slower too');
});

// The desktop's DisintegrateOverlay::setFollow: a control revealed beside a sibling is
// photographed where it sits, and the sibling's slot then pushes it along the row (user report).
test('a revealed group\'s cloud FOLLOWS the group while a sibling\'s slot moves it', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout', 'Date'] });
  dust.reset();
  let at = rect(40, 10, 120, 24);
  const el = boxEl(() => at);
  el.style.display = 'none';
  revealControls(el, true);
  const host = el.__dustHost;
  at = rect(90, 30, 30, 24);   // pushed along, and mid-slide: max-width still opening
  dust.frame();
  // Re-anchored per painted frame, left/top only: the cloud keeps the natural box's size.
  assert.deepEqual([host.style.left, host.style.top, host.style.width], ['90px', '30px', '120px']);
  // Stops on display:none (an all-zero rect would park the cloud at the page corner)…
  at = rect(0, 0, 0, 0);
  dust.frame();
  assert.deepEqual([host.style.left, host.style.top], ['90px', '30px']);
  assert.equal(dust.frames.length, 0, 'and asks for no further frame');
  // …and at the end of the flight, the way out included.
  at = rect(40, 10, 120, 24);
  revealControls(el, false);
  const out = el.__dustHost;
  t.mock.timers.tick(REVEAL_GROUP_OUT_MS);
  at = rect(300, 300, 120, 24);
  dust.frame();
  assert.equal(out.style.left, '40px', 'a finished flight is no longer followed');
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
  dust.reset();
  const menu = boxEl(rect(0, 0, 80, 60));
  disintegrate(menu, {});
  const first = menu.__dustHost;
  disintegrate(menu, {});
  assert.equal(first.parentNode, null, 'an owned cloud replaces the one before');
  const word = boxEl(rect(0, 0, 60, 18));
  markSwap(word, () => {});
  const layers = dust.clouds().filter((h) => h !== menu.__dustHost);
  assert.deepEqual(layers.map((h) => h.className.split(' ')[1]), ['dust-falling', 'dust-forming']);
  assert.equal(word.__dustHost, layers[1], 'the arrival is the flight the element owns');
  assert.ok(layers.every((h) => h.parentNode), 'and the leaving cloud is still in the air');
});

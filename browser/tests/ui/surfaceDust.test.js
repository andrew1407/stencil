// Overlay surfaces made of sand (js/ui/motion.js surfaceIn / surfaceOut): the flight
// arithmetic, the dock-away point, the speck grain and the reduced-motion/settle contract.
import test from 'node:test';
import assert from 'node:assert';

import {
  surfaceMotion, dockAwayPoint, reshapeGrid, settleSurface,
  surfaceIn, surfaceOut, surfaceDust, cancelDust,
  SURFACE_IN_MS, SURFACE_OUT_MS, SURFACE_COLS, SURFACE_ROWS, SURFACE_MOTE_PX, SURFACE_SPECK_PX,
  SURFACE_FORMING_CLASS, SURFACE_LEAVING_CLASS,
  SURFACE_DRIVEN_CLASS, MOTE_PX,
} from '../../js/ui/motion.js';
import { ANIMATIONS_CSS } from '../helpers/css.js';
import { installDustPage, rect } from '../helpers/dustRig.js';

const animCss = ANIMATIONS_CSS;

const BOX = { left: 400, top: 200, width: 600, height: 400 };
const ICON = { x: 60, y: 40 };

// ── 1. The flight ───────────────────────────────────────────────────────────

test('every mote is aimed at the point, from its own cell — that is the "from the icon"', () => {
  const cols = 10;
  const rows = 8;
  for (const [cx, cy] of [[0, 0], [5, 3], [9, 7]]) {
    const m = surfaceMotion(cx, cy, cols, rows, BOX, ICON);
    const mx = BOX.left + (cx + 0.5) * (BOX.width / cols);
    const my = BOX.top + (cy + 0.5) * (BOX.height / rows);
    // The offset lands the mote on the icon, give or take the fan (SURFACE_SPREAD/2).
    assert.ok(Math.abs(mx + m.dx - ICON.x) <= 17, `cell ${cx},${cy} arrives at the icon in x`);
    assert.ok(Math.abs(my + m.dy - ICON.y) <= 17, `cell ${cx},${cy} arrives at the icon in y`);
  }
});

test('the sweep rides the DISTANCE: the edge nearest the point goes first', () => {
  // The icon is up and to the LEFT of the box, so the top-left cell is nearest.
  const near = surfaceMotion(0, 0, 10, 8, BOX, ICON);
  const far = surfaceMotion(9, 7, 10, 8, BOX, ICON);
  assert.ok(near.delay < far.delay, 'nearest starts first');
  // …and no mote may start so late that its own flight outlives the layer.
  for (let cy = 0; cy < 8; cy++) {
    for (let cx = 0; cx < 10; cx++) {
      const m = surfaceMotion(cx, cy, 10, 8, BOX, ICON, { span: SURFACE_OUT_MS });
      assert.ok(m.delay >= 0 && m.delay <= SURFACE_OUT_MS * 0.6, `cell ${cx},${cy} starts inside the flight`);
    }
  }
});

test('the fan is deterministic and decorrelated — a cloud, not a sheet', () => {
  const a = surfaceMotion(3, 4, 10, 8, BOX, ICON);
  assert.deepEqual(a, surfaceMotion(3, 4, 10, 8, BOX, ICON), 'a hash, not Math.random');
  // Sideways drift and fall come from DIFFERENT noises (motion.js tileNoise offsets),
  // so a whole diagonal cannot move as one — the trap tileMotion's comment names.
  const spread = [];
  for (let cx = 0; cx < 10; cx++) {
    const m = surfaceMotion(cx, cx % 8, 10, 8, BOX, ICON);
    const mx = BOX.left + (cx + 0.5) * (BOX.width / 10);
    const my = BOX.top + ((cx % 8) + 0.5) * (BOX.height / 8);
    spread.push([mx + m.dx - ICON.x, my + m.dy - ICON.y]);
  }
  assert.ok(new Set(spread.map((s) => s[0])).size > 5, 'x offsets vary');
  assert.ok(new Set(spread.map((s) => s[1])).size > 5, 'y offsets vary');
  assert.ok(spread.some(([x]) => x > 0) && spread.some(([x]) => x < 0), 'and fan BOTH ways');
});

test('motes shrink to dust on the way, and each turns by its own amount', () => {
  const m = surfaceMotion(2, 2, 10, 8, BOX, ICON);
  assert.ok(m.scale > 0 && m.scale < 0.5, 'a speck by the time it arrives');
  assert.ok(Math.abs(m.rot) <= 30);
  assert.notEqual(surfaceMotion(2, 2, 10, 8, BOX, ICON).rot, surfaceMotion(3, 2, 10, 8, BOX, ICON).rot);
});

test('a degenerate box or a missing point never throws — decoration is never load-bearing', () => {
  for (const args of [[0, 0, 1, 1, null, null], [0, 0, 0, 0, BOX, null], [0, 0, 1, 1, {}, {}]]) {
    const m = surfaceMotion(...args);
    for (const v of [m.delay, m.dx, m.dy, m.rot, m.scale]) assert.ok(Number.isFinite(v));
  }
});

// ── 2. Where a docked panel's dust comes from ───────────────────────────────

test('dockAwayPoint keeps the panel’s own slide direction', () => {
  const r = { left: 0, top: 100, width: 300, height: 600 };
  assert.ok(dockAwayPoint(r, 'left').x < r.left, 'a left dock streams out to the left');
  assert.ok(dockAwayPoint(r, 'right').x > r.left + r.width, '…and a right dock to the right');
  assert.ok(dockAwayPoint(r, 'top').y < r.top);
  assert.ok(dockAwayPoint(r, 'bottom').y > r.top + r.height);
  // The cross-axis is untouched: a left dock does not also drift up or down.
  assert.equal(dockAwayPoint(r, 'left').y, r.top + r.height / 2);
  // A FLOAT has an icon to fly out of instead, so it declines and the caller resolves one.
  assert.equal(dockAwayPoint(r, 'float'), null);
  assert.equal(dockAwayPoint(null, 'left'), null);
});

// ── 3. Specks, and never clones ─────────────────────────────────────────────

test('a surface never dusts as copies of ITSELF — a cloud carries no identity', (t) => {
  // A surface's cloud lands on <body>, so it is specks and not clones: a few hundred copies of a
  // menu would answer to `.ctx-sub`, `#chat-…` and every query the app and its tests make.
  const page = installDustPage(t);
  let copies = 0;
  const el = page.entry(() => rect(400, 200, 600, 400), rect(0, 0, 2000, 2000), { cloneNode: () => { copies++; } });
  el.classList.add('ctx-sub');
  el.id = 'chat-panel';
  assert.ok(surfaceIn(el, ICON));
  const plain = page.clouds()[0];
  // (`paint` is the caller's colour override — a mark whose ink has already left the element by
  // the time it flies; it changes the COLOUR, never the specks.)
  assert.ok(surfaceDust(el, ICON, { ms: SURFACE_OUT_MS, gather: false, paint: { fill: '#f00', edge: '#00f' } }));
  assert.strictEqual(copies, 0, 'nothing hands disintegrate an element to copy');
  for (const host of [plain, page.clouds()[0]]) {
    assert.ok(!host.classList.contains('ctx-sub') && host.id !== 'chat-panel', 'the layer carries no identity');
    assert.ok(host.__cloud.motes.every((m) => m.r * 2 <= SURFACE_SPECK_PX * 1.12), 'every mote is a grain, never a cell');
  }
});

test('a surface is grained at least as fine as a row, under its own mote ceiling', () => {
  // A window is tens of times a row's area, so the BUDGET sizes its cells; the ceiling sits below
  // one per pixel, because a few thousand compositor layers read as lag.
  assert.ok(SURFACE_MOTE_PX <= MOTE_PX, 'a surface mote is no coarser than a row’s');
  // 1380 (the original ceiling) still cost ~35ms of build/style/paint on a full-height
  // docked panel, most of a close's own budget spent before the first mote had moved.
  assert.equal(SURFACE_COLS * SURFACE_ROWS, 1380, 'the surface mote ceiling — the extension’s and the desktop’s (SURFACE_MAX_CELLS)');
  // Whatever the budget leaves, the SPECK drawn in a cell is capped at a grain — a
  // cell-filling square is the "huge rectangles" a scatter must never show.
  assert.ok(SURFACE_SPECK_PX <= MOTE_PX, 'the drawn grain never grows with the cell');
  // reshapeGrid sizes motes in PIXELS and only then thins to the budget.
  const wide = reshapeGrid(SURFACE_COLS, SURFACE_ROWS, 600, 400, SURFACE_MOTE_PX);
  assert.ok(wide.cols * wide.rows <= SURFACE_COLS * SURFACE_ROWS, 'never over budget');
  assert.ok(wide.cols > wide.rows, 'and keeps the box’s aspect, so cells stay square-ish');
  // The row grain is untouched by the new parameter.
  assert.deepEqual(reshapeGrid(34, 16, 300, 60), reshapeGrid(34, 16, 300, 60, MOTE_PX));
});

// ── 4. Reduced motion, and converging ───────────────────────────────────────

const stubEl = () => {
  const classes = new Set();
  return {
    classList: {
      add: (...c) => c.forEach((x) => classes.add(x)),
      remove: (...c) => c.forEach((x) => classes.delete(x)),
      contains: (c) => classes.has(c),
    },
    style: { setProperty: () => {}, removeProperty: () => {} },
    getBoundingClientRect: () => ({ left: 0, top: 0, width: 400, height: 300 }),
    classes,
  };
};

const withReducedMotion = (fn) => {
  const prior = globalThis.matchMedia;
  globalThis.matchMedia = () => ({ matches: true });
  try { return fn(); } finally { globalThis.matchMedia = prior; }
};

test('reduced motion: nothing plays, and no class is left behind to hide the surface', () => {
  withReducedMotion(() => {
    const el = stubEl();
    assert.equal(surfaceIn(el, ICON), false);
    assert.equal(surfaceOut(el, ICON), false);
    assert.equal(el.classes.size, 0, 'no forming veil, no leaving fade, no dust-driven marker');
  });
});

test('a surface that cannot be dusted keeps the CSS entrance it always had', () => {
  // No document here, so the mote layer never builds — and the marker that would kill
  // that entrance must come straight back off.
  const el = stubEl();
  assert.equal(surfaceIn(el, ICON), false);
  assert.ok(!el.classList.contains(SURFACE_DRIVEN_CLASS));
  assert.equal(surfaceOut(el, null), false, 'and no point means no flight at all');
  assert.ok(!el.classList.contains(SURFACE_LEAVING_CLASS));
});

test('settling drops both the classes and the cloud, and is safe on anything', () => {
  const el = stubEl();
  el.classList.add(SURFACE_FORMING_CLASS, SURFACE_LEAVING_CLASS, SURFACE_DRIVEN_CLASS);
  el.__dustTimer = setTimeout(() => { throw new Error('the cancelled timer still ran'); }, 5000);
  let removed = false;
  el.__dustHost = { remove: () => { removed = true; } };
  settleSurface(el);
  assert.ok(removed, 'the layer in flight is dropped');
  assert.equal(el.__dustHost, null);
  assert.ok(!el.classList.contains(SURFACE_FORMING_CLASS));
  assert.ok(!el.classList.contains(SURFACE_LEAVING_CLASS));
  // …but the permanent marker survives: the element's old pop must stay off.
  assert.ok(el.classList.contains(SURFACE_DRIVEN_CLASS));
  settleSurface(null);        // must not throw
  cancelDust(null);
});

test('one cloud per element: a superseding open/close drops the one in the air', (t) => {
  // disintegrate() cancels before it builds, which is what stops a double-clicked menu
  // stranding a layer over the page — and every surface flight settles the element first.
  const page = installDustPage(t);
  const el = page.entry(() => rect(400, 200, 600, 400), rect(0, 0, 2000, 2000));
  surfaceIn(el, ICON);
  const first = el.__dustHost;
  surfaceOut(el, ICON);
  assert.deepStrictEqual(page.clouds(), [el.__dustHost], 'one layer in the air');
  assert.notStrictEqual(el.__dustHost, first);
  assert.ok(el.classList.contains(SURFACE_LEAVING_CLASS) && !el.classList.contains(SURFACE_FORMING_CLASS));
  assert.ok(!page.delays().some((ms) => ms === SURFACE_IN_MS + 60 || ms === SURFACE_IN_MS + 150),
    'the superseded flight\'s timers are cleared with it');
  // The layer removes itself even if nobody ever settles it.
  page.fire(SURFACE_OUT_MS + 150);
  assert.deepStrictEqual([page.clouds().length, el.__dustHost, el.__dustTimer], [0, null, null]);
  // The bare builder cancels too, with no settle in front of it.
  surfaceDust(el, ICON, { ms: SURFACE_IN_MS, gather: true });
  surfaceDust(el, ICON, { ms: SURFACE_IN_MS, gather: true });
  assert.strictEqual(page.clouds().length, 1, 'disintegrate drops the cloud it supersedes');
});

test('the box is measured with its own entrance suppressed, or every mote is icon-sized', (t) => {
  // modalFromIcon / chatSlide* / menuPop all FILL an icon-sized from-state, so a rect
  // read under them is the icon's box. The marker goes on first and kills it.
  const page = installDustPage(t);
  const driven = [];
  const el = page.entry(() => { driven.push(el.classList.contains(SURFACE_DRIVEN_CLASS)); return rect(400, 200, 600, 400); },
    rect(0, 0, 2000, 2000));
  assert.ok(surfaceIn(el, ICON));
  assert.ok(driven.length && driven.every(Boolean), 'the marker precedes every measure');
  assert.match(animCss, /\.dust-driven \{ animation: none !important; \}/);
});

// Overlay surfaces made of sand (js/ui/motion.js surfaceIn / surfaceOut).
//
// A modal, the chat panel and the ⋯/context popups used to grow out of the icon that
// opened them as a plain scale. They now play the SAME scatter a deleted chat row or
// project row plays — one clone (or one speck) per grid cell, the same deterministic
// noise, the same keyframes — only every mote flies INTO, or out of, the point that
// owns the surface. The origin and the direction are exactly what they were; the
// rendering is what changed.
//
// What is pinned here:
//   1. the flight arithmetic (pure: surfaceMotion, dockAwayPoint, reshapeGrid);
//   2. the CSS contract — the old pop/slide is OFF for good, the dust owns the box's
//      opacity, and nothing plays at all under reduced motion;
//   3. the wiring on each surface, including that the origin point is still the icon
//      (or the click, or the dock edge) and that the removal never waits on the effect.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

import {
  surfaceMotion, dockAwayPoint, reshapeGrid, settleSurface,
  surfaceIn, surfaceOut, cancelDust, tileNoise,
  SURFACE_IN_MS, SURFACE_OUT_MS, SURFACE_COLS, SURFACE_ROWS, SURFACE_MOTE_PX, SURFACE_SPECK_PX,
  SURFACE_FORMING_CLASS, SURFACE_LEAVING_CLASS,
  SURFACE_DRIVEN_CLASS, MOTE_PX,
} from '../js/ui/motion.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const animCss = read('../css/animations.css');
const baseJs = read('../js/ui/base.js');
const chatPanelJs = read('../js/ui/chatPanel.js');
const ctxJs = read('../js/ui/contextMenu.js');
const projectsJs = read('../js/ui/projectsModal.js');
const chatViewJs = read('../js/ui/chatView.js');
const motionJs = read('../js/ui/motion.js');

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

test('a surface never dusts as copies of ITSELF — a cloud carries no identity', () => {
  // A row scatter can afford real clones (a list row is one element in one place), but a
  // surface's cloud lands on <body>: a few hundred copies of a menu would be a few
  // hundred more elements answering to `.accent-dd-menu`, `.ctx-sub`, `#chat-…`, and
  // every query on the page — the app's own, and every test that drives it — would have
  // to know about a decoration. This is the guard against that coming back.
  const dust = motionJs.slice(motionJs.indexOf('const surfaceDust ='));
  assert.match(dust, /paintTile: speckPainter\(el\),/, 'a surface always paints specks');
  assert.ok(!/makeCopy/.test(dust.slice(0, dust.indexOf('settleSurface'))),
    'and never hands disintegrate an element to copy');
  assert.ok(!/cloneForTile|cloneNode/.test(motionJs.slice(motionJs.indexOf('// ── Surfaces'),
                                                          motionJs.indexOf('// ── Hover popups'))),
    'nothing in the surface section clones a node');
});

test('a surface is grained at least as fine as a row, under its own mote ceiling', () => {
  // A window is tens of times a row's area, so the BUDGET is what sizes its cells — and
  // at the wipe's own 1200 a settings window came apart into 20px slabs, a mosaic
  // rather than sand. The grain aimed for is the row's or finer.
  assert.ok(SURFACE_MOTE_PX <= MOTE_PX, 'a surface mote is no coarser than a row’s');
  assert.equal(SURFACE_COLS * SURFACE_ROWS, 2400, 'the surface mote ceiling');
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

test('one cloud per element: a superseding open/close drops the one in the air', () => {
  // disintegrate() cancels before it builds, which is what stops a double-clicked menu
  // stranding a layer over the page.
  const body = motionJs.slice(motionJs.indexOf('export function disintegrate'));
  assert.match(body, /cancelDust\(el\);\s*\/\/ one cloud per element/);
  // …and every surface flight settles the element first.
  assert.match(motionJs, /const playSurface = \(el, point, \{ ms, gather \}\) => \{\s*\n\s*if \(!el\?\.classList\) return false;\s*\n\s*settleSurface\(el\);/);
  // The layer removes itself even if nobody ever settles it.
  assert.match(body, /el\.__dustTimer = setTimeout\(\(\) => \{[\s\S]*?host\.remove\(\);/);
});

test('the box is measured with its own entrance suppressed, or every mote is icon-sized', () => {
  // modalFromIcon / chatSlide* / menuPop all FILL an icon-sized from-state, so a rect
  // read under them is the icon's box. The marker goes on first and kills it.
  const play = motionJs.slice(motionJs.indexOf('const playSurface ='), motionJs.indexOf('export const surfaceIn'));
  assert.ok(play.indexOf('classList.add(SURFACE_DRIVEN_CLASS)') < play.indexOf('surfaceDust('),
    'the marker precedes the measure');
  assert.match(animCss, /\.dust-driven \{ animation: none !important; \}/);
});

// ── 5. The CSS contract ─────────────────────────────────────────────────────

test('the surface keyframes are the row’s scatter, re-timed and re-aimed', () => {
  // Same tile vars — so the flight arithmetic above is what the browser plays.
  for (const name of ['tileGatherSurface', 'tileScatterSurface']) {
    const frames = animCss.match(new RegExp(`@keyframes ${name} \\{([\\s\\S]*?)\\n\\}`));
    assert.ok(frames, `${name} exists`);
    assert.match(frames[1], /translate\(var\(--dx[^)]*\), var\(--dy[^)]*\)\)\s*rotate\(var\(--rot[^)]*\)\)\s*scale\(var\(--tile-scale[^)]*\)\)/);
  }
  // A surface's motes are visible from the first frame — they ARE the window.
  assert.match(animCss, /@keyframes tileGatherSurface \{\s*\n\s*0%\s*\{ opacity: 0\.\d+;/);
  // …and both ride the surface's own clock, not the row's 0.9s fall.
  assert.match(animCss, /animation: tileScatter var\(--dust-ms, 0\.9s\)/);
  assert.match(animCss, /animation: tileGather var\(--gather-ms, 0\.48s\)/);
});

test('the surface waits behind its dust, and hands over to it on the way out', () => {
  assert.match(animCss, /@keyframes surfaceForm\s+\{ 0%, \d+% \{ opacity: 0; \} 100% \{ opacity: 1; \} \}/);
  assert.match(animCss, /@keyframes surfaceLeave \{ 0% \{ opacity: 1; \} \d+%, 100% \{ opacity: 0; \} \}/);
  assert.match(animCss, /\.dust-driven\.surface-forming \{ animation: surfaceForm var\(--dust-ms, \d+ms\) linear both !important; \}/);
  assert.match(animCss, /\.dust-driven\.surface-leaving \{ animation: surfaceLeave var\(--dust-ms, \d+ms\) linear both !important; \}/);
  // The cloud cross-fades with the surface at each hand-over, so a gather cannot end on
  // a flat slab of the panel's colour and a scatter cannot start on a hard cut.
  assert.match(animCss, /\.disintegrate-host\.dust-leaving \{ animation: dustHostIn /);
  assert.match(animCss, /\.disintegrate-host\.dust-forming \{ animation: dustHostOut /);
});

test('the layer can never take a click or hold focus, and never moves the page', () => {
  const host = animCss.match(/\.disintegrate-host \{([\s\S]*?)\n\}/)[1];
  assert.match(host, /position: fixed;/, 'out of flow — no reflow, ever');
  assert.match(host, /pointer-events: none;/);
  // Motes are the tiles themselves — <div>s with no text and no tabindex (motion.js
  // speckPainter paints them), so there is nothing focusable in the layer at all.
  assert.match(motionJs, /tile\.classList\.add\('dust-mote'\);/);
  assert.match(motionJs, /const tile = document\.createElement\('div'\);/);
  // …and a painted tile carries no child at all: one node per grain, not two, which is
  // what a window-sized cloud can actually afford to build in a frame.
  assert.match(motionJs, /if \(paintTile\) \{\s*\n\s*paintTile\(tile,[\s\S]{0,200}?continue;/);
});

test('reduced motion: no cloud, no veil — the surface simply is, or is not', () => {
  assert.match(animCss, /@media \(prefers-reduced-motion: reduce\) \{\s*\n\s*\.disintegrate-host \{ display: none; \}/);
  assert.match(animCss, /\.dust-driven, \.dust-driven\.surface-forming, \.dust-driven\.surface-leaving \{ animation: none !important; \}/);
});

// ── 6. Per surface: the origin is still the icon ────────────────────────────

test('modals: the dust point IS the icon centre setOriginVars already measured', () => {
  assert.match(baseJs, /originPoint = \{ x: cx, y: cy \};/);
  // The very same cx/cy that feed --modal-dx/dy, so the flight cannot drift from the
  // old one: an on-screen opener's centre, or above the box when it is unreachable.
  assert.match(baseJs, /const cx = onScreen \? a\.left \+ a\.width \/ 2 : b\.left \+ b\.width \/ 2;/);
  assert.match(baseJs, /const cy = onScreen \? a\.top \+ a\.height \/ 2 : -Math\.max\(48, b\.height \* 0\.3\);/);
  // Both shapes of open play it, and the close plays it measured while still open.
  assert.match(baseJs, /overlay\.classList\.add\('modal-open'\);[\s\S]{0,200}if \(!reducedMotion\(\) && setOriginVars\(\)\) playDust\(true\);/);
  assert.match(baseJs, /overlay\.classList\.add\('modal-open', 'modal-popover'\);[\s\S]*?if \(!reducedMotion\(\) && setOriginVars\(\)\) playDust\(true\);/);
  assert.match(baseJs, /overlay\.classList\.add\('modal-closing'\);\s*\n\s*playDust\(false\);/);
  // The window still goes away on its own clock — the close never waits on the effect.
  assert.match(baseJs, /const CLOSE_MS = SURFACE_OUT_MS;/);
  assert.match(baseJs, /settleSurface\(boxOf\(\)\);\s*\/\/ nothing plays/);
});

test('the chat panel keeps its dock edge, and a float keeps its icon', () => {
  assert.match(chatPanelJs, /if \(host\.classList\.contains\('chat-dock-float'\)\) \{[\s\S]*?anchorBtn\(\)/,
    'a float flies out of the toolbar icon, like a modal');
  assert.match(chatPanelJs, /return dockAwayPoint\(r, dock\) \|\| \{ x: r\.left \+ r\.width \/ 2, y: -Math\.max\(48, r\.height \* 0\.3\) \};/);
  // Opened AFTER the class, or the panel is display:none and measures nothing.
  assert.match(chatPanelJs, /host\.classList\.toggle\('chat-open', on\);[\s\S]{0,160}if \(on\) playDust\(true\);/);
  // Closed BEFORE it leaves the screen, and on the same clock the class swap uses.
  assert.match(chatPanelJs, /playDust\(false\);\s*\/\/ …measured while it is still on screen\s*\n\s*host\.classList\.add\('chat-closing'\);/);
  assert.match(chatPanelJs, /ms: enter \? 420 : closeMs\(\)/);
});

test('the context menu forms out of the very click it was opened at', () => {
  assert.match(ctxJs, /const openAt = \(x, y\) => \{\s*\n\s*openPoint = \{ x, y \};/);
  assert.match(ctxJs, /if \(!motionReduced\(\)\) surfaceIn\(menu, openPoint, \{ ms: SURFACE_MENU_IN_MS \}\);/);
  // Measured while still open, and only when it IS — closeMenu is also the idle teardown.
  assert.match(ctxJs, /if \(menu\.classList\.contains\('ctx-open'\) && !motionReduced\(\)\) surfaceOut\(menu, openPoint, \{ ms: SURFACE_MENU_OUT_MS \}\);/);
  assert.ok(ctxJs.indexOf('surfaceOut(menu, openPoint)') < ctxJs.indexOf("menu.classList.remove('ctx-open')"));
  // The pop it replaces set a transform, which would have made the menu the containing
  // block for its position:fixed flyouts. The dust drives opacity only.
  assert.ok(!/surface-forming[\s\S]*transform/.test(animCss.slice(animCss.indexOf('@keyframes surfaceForm'),
    animCss.indexOf('@keyframes surfaceLeave'))), 'surfaceForm animates opacity alone');
});

test('the ⋯ overflow menus grow out of the button (or the right-click) that opened them', () => {
  // Projects: the cursor for a right-click, the "⋯" button's centre otherwise.
  assert.match(projectsJs, /menuPoint = point \|\| \(ar \? \{ x: ar\.left \+ ar\.width \/ 2, y: ar\.top \+ ar\.height \/ 2 \} : null\);/);
  assert.match(projectsJs, /surfaceIn\(menu, menuPoint, \{ ms: SURFACE_MENU_IN_MS \}\);/);
  // …and back into it. The node still goes NOW: the layer owns its own lifetime.
  assert.match(projectsJs, /surfaceOut\(openMenu, menuPoint, \{ ms: SURFACE_MENU_OUT_MS \}\);\s*\n\s*openMenu\.remove\(\);/);
  // The chat bubble's "⋯" is cursor-anchored, and uses the same open point both ways.
  assert.match(chatViewJs, /surfaceIn\(menu, \{ x, y \}, \{ ms: SURFACE_MENU_IN_MS \}\);/);
  assert.match(chatViewJs, /surfaceOut\(menu, \{ x, y \}, \{ ms: SURFACE_MENU_OUT_MS \}\);\s*\n\s*menu\.remove\(\);/);
});

test('a surface forms slower than it leaves — arriving is the half you watch', () => {
  assert.ok(SURFACE_IN_MS > SURFACE_OUT_MS, 'the gather is the slower half');
  assert.ok(SURFACE_IN_MS >= 560 && SURFACE_IN_MS <= 700, 'slow enough to read as sand gathering');
  // tileNoise is the shared hash — the surface flight is the row's, not a second system.
  assert.equal(typeof tileNoise(1, 2), 'number');
  assert.equal(tileNoise(1, 2), tileNoise(1, 2));
});

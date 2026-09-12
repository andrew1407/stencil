// Overlay surfaces made of sand (js/ui/motion.js surfaceIn / surfaceOut).
//
// A modal, the chat panel and the ⋯/context popups used to grow out of the icon that
// opened them as a plain scale. They now play the SAME scatter a deleted chat or project
// row plays — one clone (or speck) per grid cell, the same deterministic noise, the same
// keyframes — only every mote flies INTO, or out of, the point that owns the surface.
//
// What is pinned here:
//   1. the flight arithmetic (pure: surfaceMotion, dockAwayPoint, reshapeGrid);
//   2. the CSS contract — the old pop/slide is OFF for good, the dust owns the box's
//      opacity, and nothing plays at all under reduced motion;
//   3. the wiring on each surface: the origin point is still the icon (or the click, or
//      the dock edge), and the removal never waits on the effect.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { motionSource } from './helpers/motionSource.js';

import {
  surfaceMotion, dockAwayPoint, reshapeGrid, settleSurface, foldBox, FOLD_INSTANT_CLASS,
  surfaceIn, surfaceOut, cancelDust, tileNoise,
  SURFACE_IN_MS, SURFACE_OUT_MS, SURFACE_COLS, SURFACE_ROWS, SURFACE_MOTE_PX, SURFACE_SPECK_PX,
  SURFACE_FORMING_CLASS, SURFACE_LEAVING_CLASS,
  SURFACE_DRIVEN_CLASS, MOTE_PX,
} from '../js/ui/motion.js';
import { FLIGHTS, alphaAt } from '../js/ui/dustCloud.js';
import { ANIMATIONS_CSS } from './helpers/css.js';
import { chatViewSource } from './helpers/chatViewSource.js';
import { contextMenuSource } from './helpers/contextMenuSource.js';
import { modalShellSource } from './helpers/modalShellSource.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const cloudJs = read('../js/ui/dustCloud.js');
const animCss = ANIMATIONS_CSS;
const baseJs = modalShellSource();
const chatPanelJs = read('../js/ui/chatPanel.js');
const confirmJs = read('../js/ui/confirmModal.js');
const ctxJs = contextMenuSource();
const rowMenuJs = read('../js/ui/projectRowMenu.js');
const chatViewJs = chatViewSource();
const llmSettingsJs = read('../js/ui/llmSettingsModal.js');
const motionJs = motionSource();
const toolbarJs = read('../js/ui/toolbar.js');
const mainContentJs = read('../js/ui/mainContent.js');

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
  // (`paint` is the caller's colour override — a mark whose ink has already left the
  // element by the time it flies; it changes the COLOUR, never the specks.)
  assert.match(dust, /paintTile: speckPainter\(el, paint\),/, 'a surface always paints specks');
  assert.ok(!/makeCopy/.test(dust.slice(0, dust.indexOf('settleSurface'))),
    'and never hands disintegrate an element to copy');
  assert.ok(!/cloneForTile|cloneNode/.test(motionJs.slice(motionJs.indexOf('// ── Surfaces'),
                                                          motionJs.indexOf('// ── Hover popups'))),
    'nothing in the surface section clones a node');
});

test('a surface is grained at least as fine as a row, under its own mote ceiling', () => {
  // A window is tens of times a row's area, so the BUDGET is what sizes its cells. The
  // grain aimed for is the row's or finer; the CEILING is lower than "one per pixel" would
  // want, because a few thousand compositor layers read as lag on a big surface. The speck
  // stays capped separately, so a coarser grid is still sand.
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

test('one cloud per element: a superseding open/close drops the one in the air', () => {
  // disintegrate() cancels before it builds, which is what stops a double-clicked menu
  // stranding a layer over the page.
  const body = motionJs.slice(motionJs.indexOf('export function disintegrate'));
  assert.match(body, /\/\/ One cloud per element[\s\S]{0,320}?if \(own\) cancelDust\(el\);/);
  // …and every surface flight settles the element first.
  // Options are pinned by NAME, not as a verbatim parameter list — the list grows
  // (delayScale), and a signature-shaped regex only says the file was edited.
  assert.match(motionJs, /const playSurface = \(el, point, \{[^}]*\bbox = null\b[^}]*\}\) => \{\s*\n\s*if \(!el\?\.classList\) return false;\s*\n\s*settleSurface\(el\);/);
  // The layer removes itself even if nobody ever settles it.
  assert.match(body, /const life = setTimeout\(\(\) => \{[\s\S]*?host\.remove\(\);/);
  assert.match(body, /if \(own\) \{ el\.__dustHost = host; el\.__dustTimer = life; \}/);
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

test('the surface flights are the row’s scatter, re-timed and re-aimed', () => {
  // Same grain, same waypoint arithmetic as a row's (dustCloud.js FLIGHTS): a gather
  // starts at the far end and flies home, a scatter the other way.
  assert.equal(FLIGHTS.surfaceGather.from, 'far');
  assert.equal(FLIGHTS.surfaceScatter.from, 'home');
  // A surface's motes are visible from the first frame — they ARE the window.
  assert.equal(alphaAt(FLIGHTS.surfaceGather.alpha, 0), 0.55);
  assert.equal(alphaAt(FLIGHTS.surfaceScatter.alpha, 0), 1);
  // …and their ease-out covers most of the trip early, so the bend sits early too.
  assert.ok(FLIGHTS.surfaceGather.split <= 0.2 && FLIGHTS.surfaceScatter.split <= 0.2);
  assert.ok(FLIGHTS.surfaceGather.rest(0.3) > 0.8, 'ease-out: most of the trip in the first third');
  // Both ride the surface's own clock (motion.js writes --dust-ms / --gather-ms on the
  // host for its cross-fades, and each grain's duration inline).
  assert.match(motionJs, /const gatherMs = toward \|\| !gather \? span : Math\.round\(span \* TILE_GATHER_SHARE\);/);
  assert.match(motionJs, /host\.style\.setProperty\('--gather-ms', `\$\{gatherMs\}ms`\);/);
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
  // The motes are pixels on ONE canvas inside the layer (dustCloud.js) — no node per
  // grain, nothing with text or a tabindex, so there is nothing focusable at all.
  assert.match(motionJs, /startCloud\(host, motes, \{ flight: kind, span, colours: fills, origin/);
  assert.match(cloudJs, /canvas\.style\.cssText = `position:absolute;[\s\S]{0,200}?pointer-events:none;`/);
  assert.match(cloudJs, /host\.appendChild\(canvas\);/);
  assert.ok(!/disintegrate-tile/.test(motionJs), 'no node per grain any more');
  // The layer never waits on the paint: a document without a 2D canvas (tests) still
  // gets the bookkeeping, and the loop is stopped before the layer goes.
  assert.match(cloudJs, /const ctx = canvas\.getContext\?\.\('2d'\);\s*\n\s*if \(!ctx\) return noop;/);
  assert.match(motionJs, /el\.__dustHost\?\.__stop\?\.\(\);/);
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
  assert.match(baseJs, /const MODAL_CLOSE_MS = SURFACE_OUT_MS;/);
  assert.match(baseJs, /flight\.settle\(\);\s*\/\/ nothing plays/);
  // …and the CONFIRM dialog, which has no opener icon at all, plays the very same
  // flight out of the gesture that raised it (ui/gesturePoint.js).
  assert.match(confirmJs, /createModalFlight\(overlay, \(\) => overlay\.querySelector\('\.app-modal'\)\)/);
  assert.match(confirmJs, /openAnchor = gestureAnchorRect\(\);[\s\S]{0,200}if \(!flight\.reducedMotion\(\) && flight\.setOrigin\(openAnchor\)\) flight\.playDust\(true\);/);
  assert.match(confirmJs, /if \(animate\) flight\.playClosing\(\);/);
  // The close reuses the same anchor the dialog opened with, not a fresh
  // gestureAnchorRect() (which by then is wherever Confirm was clicked) — unless the
  // caller named a closeAnchor, since a context-menu row is gone by the time it lands.
  assert.match(confirmJs, /flight\.setOrigin\(rectOf\(closeAnchorEl\) \|\| openAnchor\);\s*\n\s*overlay\.classList\.remove\('modal-open'\);/);
  assert.ok(!/const settle = \(val\) => \{[\s\S]{0,200}gestureAnchorRect\(\)/.test(confirmJs),
    'settle() must not recompute the gesture point — it would anchor on the button that just closed it');
});

test('the chat panel keeps its dock edge, and a float keeps its icon', () => {
  assert.match(chatPanelJs, /if \(host\.classList\.contains\('chat-dock-float'\)\) \{[\s\S]*?anchorBtn\(\)/,
    'a float flies out of the toolbar icon, like a modal');
  assert.match(chatPanelJs, /return dockAwayPoint\(r, chatDock\.mode\(\)\) \|\| \{ x: r\.left \+ r\.width \/ 2, y: -Math\.max\(48, r\.height \* 0\.3\) \};/);
  // Opened AFTER the class, or the panel is display:none and measures nothing.
  assert.match(chatPanelJs, /host\.classList\.toggle\('chat-open', on\);[\s\S]{0,160}if \(on\) playDust\(true\);/);
  // Closed BEFORE it leaves the screen, and on the same clock the class swap uses.
  assert.match(chatPanelJs, /playDust\(false\);\s*\/\/ …measured while it is still on screen\s*\n\s*host\.classList\.add\('chat-closing'\);/);
  assert.match(chatPanelJs, /ms: enter \? 420 : CLOSE_MS/);
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
  // Projects (ui/projectRowMenu.js): the cursor for a right-click, else the "⋯" centre — and
  // back into it; the node still goes NOW, the layer owns its own lifetime.
  assert.match(rowMenuJs, /menuPoint = point \|\| rectCenter\(anchor\);/);
  assert.match(rowMenuJs, /surfaceIn\(menu, menuPoint, \{ ms: SURFACE_MENU_IN_MS \}\);/);
  assert.match(rowMenuJs, /surfaceOut\(openMenu, menuPoint, \{ ms: SURFACE_MENU_OUT_MS \}\);\s*\n\s*openMenu\.remove\(\);/);
  // The chat bubble's "⋯" is cursor-anchored, same open point both ways.
  assert.match(chatViewJs, /surfaceIn\(menu, \{ x, y \}, \{ ms: SURFACE_MENU_IN_MS \}\);/);
  assert.match(chatViewJs, /surfaceOut\(menu, \{ x, y \}, \{ ms: SURFACE_MENU_OUT_MS \}\);\s*\n\s*menu\.remove\(\);/);
});

// The composer's "…" menu was the last popup in the app still hard-cutting on both
// edges — every other surface already flew. It carries the settings/clear/attach items,
// so it is also the one the user opens most.
test('the composer "…" menu forms out of, and pours back into, its own trigger', () => {
  const wire = chatViewJs.slice(chatViewJs.indexOf('export const wireChatMoreMenu'));
  // The point is the trigger's centre, measured live (the composer moves with the dock).
  assert.match(wire, /const dustPoint = \(\) => rectCenter\(btn\);/);
  assert.match(wire, /surfaceIn\(menu, dustPoint\(\), \{ ms: SURFACE_MENU_IN_MS \}\)/);
  assert.match(wire, /surfaceOut\(menu, dustPoint\(\), \{ ms: SURFACE_MENU_OUT_MS \}\)/);
  // Only a real change flies: re-closing a closed menu must not raise a second cloud.
  assert.match(wire, /const changed = on === menu\.hidden;/);
  // `hidden` is display:none, so the box must still be up when the flight is played,
  // and must go straight after — the cloud is a copy on <body> with its own life.
  const open = wire.indexOf('menu.hidden = false;');
  const out = wire.indexOf('surfaceOut(menu, dustPoint()');
  const hide = wire.indexOf('if (!on) menu.hidden = true;');
  assert.ok(open < wire.indexOf('surfaceIn(menu, dustPoint()'), 'unhidden before it forms');
  assert.ok(out < hide, 'measured while still up, hidden immediately after');
});

// The gear that raises the settings window lives INSIDE that menu, and the menu closes
// as it is clicked — so measuring the gear gives 0x0 and the window used to fall from
// above. It belongs to the "…" the user can still see.
test('the assistant settings window flies to the "…", not to the gear that vanished', () => {
  assert.match(llmSettingsJs, /originEl: \(\) => visibleChatMoreBtn\(\)/);
  const pick = chatViewJs.slice(chatViewJs.indexOf('export const visibleChatMoreBtn'));
  // The composer the user last opened wins, then whichever surface is actually up.
  assert.match(pick, /shownBtn\(lastMoreBtn\)/);
  assert.match(pick, /shownBtn\(doc\.getElementById\('chat-more-btn'\)\)/);
  assert.match(pick, /shownBtn\(doc\.getElementById\('ctx-assist-more-btn'\)\)/);
  // A hidden trigger measures 0x0 and must NOT be offered — null is the honest answer,
  // and base.js then falls from above (its `onScreen` guard).
  assert.match(chatViewJs, /const shownBtn = \(el\) => \{[\s\S]{0,200}r\.width > 0 && r\.height > 0 \? el : null;/);
  assert.match(pick, /\|\| null;/);
  // The shell resolves it at BOTH edges, because close() re-measures before it plays.
  assert.match(baseJs, /const defaultOrigin = \(\) => \(originFor \? originFor\(\) : openBtn\);/);
  assert.match(baseJs, /: \(from === null \? null : defaultOrigin\(\)\);/);
});

test('a FOLD is the exception: it leaves slower than it arrives', async () => {
  const { FOLD_DUST_OUT_MS } = await import('../js/ui/motion.js');
  assert.ok(FOLD_DUST_OUT_MS > SURFACE_OUT_MS,
    'a fold has no icon to shrink into — the fold IS the close, so it may not be brisk');
  const token = (name) => Number(/(\d+)ms/.exec(new RegExp(`--${name}:\\s*([^;]+);`).exec(animCss)[1])[1]);
  // The sand and the CSS fold must scale together, or one outlives the other.
  assert.equal((FOLD_DUST_OUT_MS / SURFACE_OUT_MS).toFixed(2),
               (token('fold-out-ms') / token('fold-ms')).toFixed(2));
});

test('a surface forms slower than it leaves — arriving is the half you watch', () => {
  assert.ok(SURFACE_IN_MS > SURFACE_OUT_MS, 'the gather is the slower half');
  assert.ok(SURFACE_IN_MS >= 560 && SURFACE_IN_MS <= 800, 'slow enough to read as sand gathering, brisk enough not to wait on');
  // tileNoise is the shared hash — the surface flight is the row's, not a second system.
  assert.equal(typeof tileNoise(1, 2), 'number');
  assert.equal(tileNoise(1, 2), tileNoise(1, 2));
});


// ── 5. The two FOLDING surfaces ─────────────────────────────────────────────
// The toolbar's tool rows and the points panel's table have no opener icon: CSS owns their
// whole reveal (grid-template-rows / width), so at the moment the toggle flips they are
// still at the box they are LEAVING — zero on the way in. foldBox gives the flight a box.

// A fake element/scope pair: the box it reports depends on whether `cls` is set, exactly
// as the real fold's does.
const foldStub = (cls, openBox, shutBox = { width: 0, height: 0, left: 0, top: 0 }) => {
  const classes = new Set([cls]);
  const reads = [];
  const el = {
    classList: {
      add: (c) => classes.add(c), remove: (c) => classes.delete(c),
      contains: (c) => classes.has(c),
      toggle: (c, on) => (on ? classes.add(c) : classes.delete(c)),
    },
    getBoundingClientRect: () => {
      const box = classes.has(cls) ? shutBox : openBox;
      reads.push({ box, instant: classes.has(FOLD_INSTANT_CLASS) });
      return box;
    },
  };
  return { el, classes, reads };
};

test('foldBox reads the OPEN box, with the transitions off, and puts the state back', () => {
  const open = { left: 0, top: 0, width: 900, height: 160 };
  const { el, classes, reads } = foldStub('hidden', open);
  const box = foldBox(el, el, 'hidden', false, FOLD_INSTANT_CLASS);
  assert.deepEqual(box, { left: 0, top: 0, width: 900, height: 160 },
    'the box dusted is the one the fold is about to reach, not the collapsed one');
  // Every read happened with the fold's easing suppressed — a transitioned read hands
  // back the box it is leaving, which is the whole bug this exists for.
  assert.ok(reads.length >= 2 && reads.every((r) => r.instant), 'measured with motion off');
  // …and nothing survives the round trip: same state in, same state out.
  assert.ok(classes.has('hidden'), 'the collapsed state is restored');
  assert.ok(!classes.has(FOLD_INSTANT_CLASS), 'the escape hatch is dropped again');
});

test('foldBox declines an unmeasurable box, and never throws on a stub', () => {
  const { el } = foldStub('hidden', { left: 0, top: 0, width: 4, height: 4 });
  assert.equal(foldBox(el, el, 'hidden', false, FOLD_INSTANT_CLASS), null);
  assert.equal(foldBox(null, null, 'hidden', false, FOLD_INSTANT_CLASS), null);
  assert.equal(foldBox({}, {}, 'hidden', false, FOLD_INSTANT_CLASS), null);
});

test('a folding surface hands its box in — the live rect is the wrong one', () => {
  // playSurface/surfaceDust/disintegrate all take the override, or a fold dusts over
  // a zero-height box and the flight silently declines.
  assert.match(motionJs, /export const surfaceIn = \(el, point, \{[^}]*\bms = SURFACE_IN_MS\b[^}]*\bbox = null\b[^}]*\} = \{\}\) =>/);
  assert.match(motionJs, /export const surfaceOut = \(el, point, \{[^}]*\bms = SURFACE_OUT_MS\b[^}]*\bbox = null\b[^}]*\} = \{\}\) =>/);
  const dust = motionJs.slice(motionJs.indexOf('const surfaceDust ='));
  assert.match(dust, /const r = box \|\| el\.getBoundingClientRect\(\);/);
  const dis = motionJs.slice(motionJs.indexOf('export function disintegrate'));
  assert.match(dis, /const r = box \|\| el\.getBoundingClientRect\(\);/);
  // The suppression class is real CSS, on both folds and their fading children.
  const instant = animCss.slice(animCss.indexOf('#controls-body.fold-instant'));
  assert.match(instant.slice(0, instant.indexOf('}')), /transition: none !important/);
  for (const sel of ['#controls-body.fold-instant > *', '.coordinates-panel.fold-instant',
                     '.coordinates-panel.fold-instant #coord-body'])
    assert.ok(animCss.includes(sel), `${sel} is suppressed for the read`);
});

test('the tool rows dust up past the top edge, measured before the class flips', () => {
  // Both folds ride ONE shared ritual (motion.js foldDust): measure the SHOWN box,
  // THEN let the caller fold (the other order reads the box it is leaving), aim past
  // the dock edge, and give the collapse the fold's own slower exit clock.
  const fold = motionJs.slice(motionJs.indexOf('export function foldDust'));
  assert.ok(fold.indexOf('foldBox(el, scope, cls, false, FOLD_INSTANT_CLASS)') !== -1);
  assert.ok(fold.indexOf('foldBox(') < fold.indexOf('toggle?.();'),
    'the box is measured before the fold starts');
  assert.match(fold, /const away = box && dockAwayPoint\(box, dock\);/);
  assert.match(fold, /ms: hiding \? FOLD_DUST_OUT_MS : inMs/);
  assert.match(toolbarJs,
    /foldDust\(body, body, 'hidden', hidden, 'top',\s*\n?\s*\{ toggle: \(\) => body\.classList\.toggle\('hidden', hidden\) \}\);/);
});

test('the points table pours out past the right edge the panel collapses towards', () => {
  assert.match(mainContentJs,
    /foldDust\(body, panel, 'coord-collapsed', hidden, 'right',\s*\n?\s*\{ inMs: 460, toggle: \(\) => panel\.classList\.toggle\('coord-collapsed', hidden\) \}\);/);
  // .coord-folding takes the table out of the layout — it must be off for the read.
  assert.ok(mainContentJs.indexOf("panel.classList.remove('coord-folding')")
            < mainContentJs.indexOf('foldDust(body, panel'),
    'the fold hold is lifted before the box is read');
});

test('neither fold dusts under reduced motion — the box is not even measured', () => {
  const fold = motionJs.slice(motionJs.indexOf('export function foldDust'));
  assert.match(fold, /motionReduced\(\) \? null : foldBox\(/,
    'reduced motion skips the two forced layouts as well as the flight');
});

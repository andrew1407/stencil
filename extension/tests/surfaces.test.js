// Surfaces made of dust (src/lib/motion.js surfaceIn / surfaceOut) — the extension half
// of the shared contract; browser/tests is the twin. A menu, a flyout, a dropdown and a
// dialog no longer scale out of the icon that owns them: they form from motes streaming
// out of it and come apart into motes pouring back in, with the ORIGIN and DIRECTION the
// scale always had. Node has no DOM, so the geometry is pinned as pure functions and the
// layer itself is driven over a hand-rolled document; the CSS contract is read out of the
// stylesheets, and the injected modal (which can link none of them) out of its own source.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

import {
  reshapeGrid, surfaceMotion, centerOf, cancelDust, settleSurface,
  surfaceIn, surfaceOut, disintegrate,
  SURFACE_COLS, SURFACE_ROWS, SURFACE_MOTE_PX, SURFACE_SPREAD, SURFACE_IN_MS, SURFACE_OUT_MS,
  SURFACE_DRIVEN_CLASS, SURFACE_FORMING_CLASS, SURFACE_LEAVING_CLASS,
} from '../src/lib/motion.js';
import { FLIGHTS, alphaAt, PAINT_STOPS } from '../src/lib/dustCloud.js';

const css = (rel) => readFileSync(new URL(rel, import.meta.url), 'utf8');
const ANIMS = css('../src/lib/animations.css');
const MOTION = css('../src/lib/motion.js');
const THEME = css('../src/lib/theme.css');
const OVERLAY = css('../src/lib/overlay.js');

// ── The mesh ────────────────────────────────────────────────────────────────
// Motes are sized in PIXELS: a fixed grid over a wide box gives slivers, over a small
// one gives real dust. The quoted grid is only the frame-budget ceiling.
test('reshapeGrid aims for the mote size, not a fixed cell count', () => {
  const { cols, rows } = reshapeGrid(SURFACE_COLS, SURFACE_ROWS, 160, 240, SURFACE_MOTE_PX);
  assert.equal(cols, 27);   // 160 / 6
  assert.equal(rows, 40);   // 240 / 6
});

test('reshapeGrid holds a full-window surface to the quoted budget', () => {
  const budget = SURFACE_COLS * SURFACE_ROWS;
  const { cols, rows } = reshapeGrid(SURFACE_COLS, SURFACE_ROWS, 1040, 760, SURFACE_MOTE_PX);
  // At 8px that box wants 130x95 = 12350 cells; the scale-back brings it to the budget,
  // within the rounding of one cell each way (both axes are scaled by the same factor and
  // then rounded, so the product can land a couple of percent over — never an order of it).
  assert.ok(cols * rows <= budget * 1.05, `1040x760 comes back near the budget (got ${cols}x${rows})`);
  assert.ok(cols * rows >= budget * 0.8, 'and it actually spends the budget it has');
  assert.ok(cols >= 3 && rows >= 3);
});

test('reshapeGrid keeps three bands each way — two reads as splitting in half', () => {
  const tiny = reshapeGrid(SURFACE_COLS, SURFACE_ROWS, 9, 9, SURFACE_MOTE_PX);
  assert.deepEqual(tiny, { cols: 3, rows: 3 });
});

// ── One mote's flight ───────────────────────────────────────────────────────
const BOX = { left: 100, top: 100, width: 200, height: 120 };
const POINT = { x: 400, y: 60 };     // an icon up and to the right of the box

test('every mote is aimed at the point that owns the surface', () => {
  // The cell centre plus the mote's own (dx, dy) lands on the point, up to the fan.
  for (const [cx, cy] of [[0, 0], [9, 5], [19, 11]]) {
    const m = surfaceMotion(cx, cy, 20, 12, BOX, POINT);
    const mx = BOX.left + ((cx + 0.5) * BOX.width) / 20;
    const my = BOX.top + ((cy + 0.5) * BOX.height) / 12;
    assert.ok(Math.abs(mx + m.dx - POINT.x) <= SURFACE_SPREAD / 2 + 1, 'lands on the point in x');
    assert.ok(Math.abs(my + m.dy - POINT.y) <= SURFACE_SPREAD / 2 + 1, 'lands on the point in y');
  }
});

test('the sweep rides the DISTANCE: the near edge goes first, the far one last', () => {
  // Column 19 is the edge facing the point; column 0 is the far one.
  const near = surfaceMotion(19, 0, 20, 12, BOX, POINT).delay;
  const far = surfaceMotion(0, 11, 20, 12, BOX, POINT).delay;
  assert.ok(near < far, `near ${near}ms leads far ${far}ms`);
});

test('a mote never starts after the flight it belongs to', () => {
  for (let cy = 0; cy < 12; cy++) {
    for (let cx = 0; cx < 20; cx++) {
      const { delay } = surfaceMotion(cx, cy, 20, 12, BOX, POINT, { span: SURFACE_OUT_MS });
      assert.ok(delay >= 0 && delay < SURFACE_OUT_MS, `delay ${delay} inside the span`);
    }
  }
});

test('surfaceMotion is deterministic — the same cell always flies the same way', () => {
  assert.deepEqual(surfaceMotion(3, 4, 20, 12, BOX, POINT), surfaceMotion(3, 4, 20, 12, BOX, POINT));
  assert.notDeepEqual(surfaceMotion(3, 4, 20, 12, BOX, POINT), surfaceMotion(4, 3, 20, 12, BOX, POINT));
});

test('a degenerate box or point never produces NaN', () => {
  for (const m of [surfaceMotion(0, 0, 0, 0, null, null), surfaceMotion(0, 0, 1, 1, BOX, {})]) {
    for (const v of Object.values(m)) assert.ok(Number.isFinite(v), 'every value is a number');
  }
});

// ── What a surface's cloud is made of ───────────────────────────────────────
test('a surface never dusts as copies of ITSELF — a cloud carries no identity', () => {
  // A row scatter can afford real clones (a list row is one element in one place), but a
  // surface's cloud lands on <body>: a few hundred copies of a menu would be a few
  // hundred more elements answering to `.accent-dd-menu`, `.action-menu`, an id — and
  // every query on the page would have to know about a decoration.
  const motionJs = readFileSync(new URL('../src/lib/motion.js', import.meta.url), 'utf8');
  const dust = motionJs.slice(motionJs.indexOf('const surfaceDust ='));
  assert.match(dust, /paintTile: speckPainter\(el\),/, 'a surface always paints specks');
  assert.ok(!/makeCopy|cloneNode|cloneForTile/.test(dust.slice(0, dust.indexOf('settleSurface'))),
    'and never hands disintegrate an element to copy');
});

test('centerOf is the middle of the control, and null when there is nothing to measure', () => {
  assert.deepEqual(centerOf({ left: 10, top: 20, width: 30, height: 40 }), { x: 25, y: 40 });
  assert.deepEqual(centerOf({ getBoundingClientRect: () => ({ left: 0, top: 0, width: 8, height: 4 }) }),
    { x: 4, y: 2 });
  assert.equal(centerOf(null), null);
  assert.equal(centerOf({}), null);
});

// ── The layer itself, over a hand-rolled document ───────────────────────────
// Only the surface motion.js actually touches: classes, style, children, a rect.
const makeEl = (rect = { left: 100, top: 100, width: 200, height: 120 }, kids = 8) => {
  const classes = new Set();
  const props = {};
  const el = {
    tag: 'div', className: '', children: [], attrs: {}, inert: false, props,
    classList: {
      add: (...c) => c.forEach((x) => classes.add(x)),
      remove: (...c) => c.forEach((x) => classes.delete(x)),
      contains: (c) => classes.has(c),
      toggle: (c, on) => (on ? classes.add(c) : classes.delete(c)),
    },
    classes,
    style: {
      setProperty: (k, v) => { props[k] = v; },
      removeProperty: (k) => { delete props[k]; },
    },
    setAttribute: (k, v) => { el.attrs[k] = v; },
    appendChild: (c) => { c.parent?.removeChild?.(c); c.parent = el; el.children.push(c); return c; },
    removeChild: (c) => { const i = el.children.indexOf(c); if (i >= 0) el.children.splice(i, 1); },
    remove: () => el.parent?.removeChild?.(el),
    cloneNode: () => makeEl(rect, kids),
    getElementsByTagName: () => new Array(kids),
    getBoundingClientRect: () => ({ ...rect }),
  };
  return el;
};

// Run `fn` with a document, a window and a media preference in place.
const withDom = (fn, { reduced = false } = {}) => {
  const prior = {
    document: globalThis.document, matchMedia: globalThis.matchMedia,
    getComputedStyle: globalThis.getComputedStyle,
  };
  const body = makeEl({ left: 0, top: 0, width: 1000, height: 800 }, 0);
  globalThis.document = { body, createElement: () => makeEl({ left: 100, top: 100, width: 200, height: 120 }, 0) };
  globalThis.matchMedia = () => ({ matches: reduced });
  globalThis.getComputedStyle = () => ({ backgroundColor: 'rgb(1, 2, 3)', borderTopColor: 'rgb(4, 5, 6)' });
  try { return fn(body); } finally { Object.assign(globalThis, prior); }
};

const hosts = (body) => body.children.filter((c) => /disintegrate-host/.test(c.className));

test('opening a surface builds ONE mote layer, on <body>, aimed at the icon', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  withDom((body) => {
    const menu = makeEl();
    assert.equal(surfaceIn(menu, { x: 400, y: 60 }), true);
    const [host] = hosts(body);
    assert.ok(host, 'the layer lands on <body> — a dialog backdrop is removed under it');
    assert.match(host.className, /dust-forming/, 'the gather, not the scatter');
    assert.ok(host.__cloud.motes.length > 50, 'a real mesh of motes — on one canvas, not a node each');
    // The surface itself only ever gains classes and its clock — never a layout property.
    assert.ok(menu.classList.contains(SURFACE_DRIVEN_CLASS));
    assert.ok(menu.classList.contains(SURFACE_FORMING_CLASS));
    assert.equal(menu.props['--dust-ms'], `${SURFACE_IN_MS}ms`);
    assert.deepEqual(Object.keys(menu.props), ['--dust-ms'], 'no box, no position, no size');
  });
});

test('the mote layer can neither be clicked nor tabbed into', () => {
  withDom((body) => {
    const menu = makeEl();
    surfaceOut(menu, { x: 400, y: 60 });
    const [host] = hosts(body);
    assert.equal(host.attrs['aria-hidden'], 'true');
    assert.equal(host.inert, true, 'a cloned menu is full of real <button>s');
  });
  // …and the layer is pointer-transparent by rule, not by hope.
  assert.match(ANIMS, /\.disintegrate-host \{[^}]*pointer-events: none;/);
});

test('rapid open/close never stacks or strands a cloud — the newest gesture owns it', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  withDom((body) => {
    const menu = makeEl();
    for (let i = 0; i < 6; i++) {
      surfaceIn(menu, { x: 400, y: 60 });
      surfaceOut(menu, { x: 400, y: 60 });
    }
    assert.equal(hosts(body).length, 1, 'one cloud at a time, however fast the toggling');
    // …and the last word is the true state: settle drops the cloud AND the classes.
    settleSurface(menu);
    assert.equal(hosts(body).length, 0);
    assert.ok(!menu.classList.contains(SURFACE_FORMING_CLASS));
    assert.ok(!menu.classList.contains(SURFACE_LEAVING_CLASS));
    assert.equal(menu.props['--dust-ms'], undefined);
  });
});

test('the layer and the classes clear themselves on their own timers', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  withDom((body) => {
    const menu = makeEl();
    surfaceOut(menu, { x: 400, y: 60 });
    t.mock.timers.tick(SURFACE_OUT_MS + 60);
    assert.ok(!menu.classList.contains(SURFACE_LEAVING_CLASS), 'the surface stops driving itself');
    t.mock.timers.tick(400);
    assert.equal(hosts(body).length, 0, 'no stranded canvas, no leaked timer');
  });
});

test('reduced motion: no motes, no classes — the surface just opens and closes', () => {
  withDom((body) => {
    const menu = makeEl();
    assert.equal(surfaceIn(menu, { x: 400, y: 60 }), false);
    assert.equal(surfaceOut(menu, { x: 400, y: 60 }), false);
    assert.equal(hosts(body).length, 0);
    assert.equal(menu.classes.size, 0, 'not even the dust-driven marker is left behind');
  }, { reduced: true });
});

test('a surface with no icon point, or too small to see, declines without a trace', () => {
  withDom((body) => {
    const menu = makeEl();
    assert.equal(surfaceIn(menu, null), false);
    assert.equal(surfaceIn(menu, { x: NaN, y: 0 }), false);
    assert.equal(surfaceIn(makeEl({ left: 0, top: 0, width: 4, height: 4 }), { x: 9, y: 9 }), false);
    assert.equal(hosts(body).length, 0);
    assert.equal(menu.classes.size, 0, 'the element keeps the CSS entrance it always had');
  });
});

test('with no element at all, nothing throws and nothing is claimed', () => {
  assert.equal(surfaceIn(null, { x: 1, y: 1 }), false);
  assert.equal(surfaceOut(undefined, { x: 1, y: 1 }), false);
  assert.doesNotThrow(() => { settleSurface(null); cancelDust(null); });
});

test('a mote is a painted speck, never a copy — one canvas per cloud, no node per grain', () => {
  withDom((body) => {
    disintegrate(makeEl(), { cols: 4, rows: 4, toward: { x: 0, y: 0 }, ms: 200, toBody: true });
    const [host] = hosts(body);
    const cloud = host.__cloud;
    assert.equal(cloud.flight, 'surfaceScatter');
    assert.ok(cloud.motes.length >= 9, 'at least three bands each way');
    // Everything a grain needs rides its record: home, throw, waypoint, size, its
    // speck's own colour and opacity, and its clock.
    for (const k of ['x', 'y', 'dx', 'dy', 'mx', 'my', 'r', 's', 'a', 'w', 'delay', 'dur'])
      assert.ok(Number.isFinite(cloud.motes[0][k]), `${k} on the grain`);
    assert.equal(cloud.colours.length, PAINT_STOPS, 'painted from the theme palette, never the surface’s own pixels');
    assert.match(cloud.colours[0], /var\(--accent\) 100%/);
    assert.equal(cloud.colours.at(-1), 'var(--dust-accent-alt, #442082)', '…tints and all');
    // The spark and that accent tint follow the theme, or one of them would be invisible.
    assert.match(THEME, /--dust-ink:\s*#1f1f1f;[\s\S]*--dust-accent-alt:\s*color-mix\(in srgb, var\(--accent\) 55%, #000000\)/);
    assert.match(THEME, /--dust-ink:\s*#ffffff;[\s\S]*--dust-accent-alt:\s*color-mix\(in srgb, var\(--accent\) 30%, #ffffff\)/);
    assert.ok(!host.children.some((c) => c.classes?.has?.('disintegrate-tile')), 'no node per grain');
  });
});

// ── The CSS contract ────────────────────────────────────────────────────────
test('a dusted surface waits behind its motes, and its old pop stays off for good', () => {
  assert.match(ANIMS, /\.dust-driven \{ animation: none !important; \}/);
  assert.match(ANIMS, /\.dust-driven\.surface-forming \{ animation: stSurfaceForm var\(--dust-ms/);
  assert.match(ANIMS, /\.dust-driven\.surface-leaving \{ animation: stSurfaceLeave var\(--dust-ms/);
  // Held back while the motes gather, up as they land — and visible well before the last
  // one, so nothing you can already click stays invisible.
  assert.match(ANIMS, /@keyframes stSurfaceForm\s+\{ 0%, 55% \{ opacity: 0; \} 100% \{ opacity: 1; \} \}/);
  assert.match(ANIMS, /@keyframes stSurfaceLeave \{ 0% \{ opacity: 1; \} 16%, 100% \{ opacity: 0; \} \}/);
});

test("a surface's motes are visible from the FIRST frame, unlike a row's", () => {
  // The flights are dustCloud's table now (one particle system, shared with the browser
  // byte for byte): a surface's motes start visible, a row's from nothing.
  assert.equal(alphaAt(FLIGHTS.surfaceGather.alpha, 0), 0.55);
  assert.equal(alphaAt(FLIGHTS.surfaceScatter.alpha, 0), 1);
  assert.equal(alphaAt(FLIGHTS.gather.alpha, 0), 0);
  // A surface flies on its own, shorter clock; a row keeps the default span.
  assert.match(MOTION, /const gatherMs = toward \|\| !gather \? span : Math\.round\(span \* TILE_GATHER_SHARE\);/);
  assert.match(MOTION, /dur: gather \? gatherMs : Math\.max\(MIN_TILE_MS, span - m\.delay\)/);
  // …and the layer is one canvas, not a node per grain.
  assert.match(ANIMS, /\.disintegrate-host > canvas \{ position: absolute; display: block; \}/);
  assert.ok(!/disintegrate-tile/.test(ANIMS) && !/@keyframes stTile/.test(ANIMS), 'no rule left per tile');
});

test('reduced motion neutralises the surface classes the preference may have flipped under', () => {
  const tail = ANIMS.slice(ANIMS.indexOf('.disintegrate-host.dust-forming {'));
  assert.match(tail, /@media \(prefers-reduced-motion: reduce\) \{[\s\S]*?\.dust-driven, \.dust-driven\.surface-forming, \.dust-driven\.surface-leaving \{ animation: none !important; \}/);
  assert.match(ANIMS, /\.disintegrate-host \{ display: none; \}/, 'no motes at all under the preference');
});

test('the tooltip keeps its mask grain as the fallback under the real motes', () => {
  // The tooltip forms and leaves as PARTICLES now, like every other surface. The three
  // coprime dot grids ramped by --dissolve stay as the rest state and the reduced-motion
  // / declined-dust fallback; `.dust-driven` steps them aside once real motes have flown.
  assert.match(THEME, /#app-tooltip \{[\s\S]*?--dissolve: 1;/);
  assert.match(THEME, /#app-tooltip\.visible \{[\s\S]*?--dissolve: 0;/);
  assert.match(THEME, /mask-size: 4px 4px, 7px 7px, 11px 11px;/);
  assert.match(THEME, /mask-position: 0 0, 2px 3px, 5px 1px;/);
  assert.match(THEME, /mask-composite: add;/);
  // At 120% the dots overlap outright, so a SETTLED tooltip is solid to the pixel —
  // decoration must never cost legibility.
  assert.match(THEME, /#000 max\(0%, calc\(120% - var\(--dissolve\) \* 160%\)\)/);
  // --dissolve can only be transitioned because animations.css registers it.
  assert.match(ANIMS, /@property --dissolve \{ syntax: "<number>"; inherits: false; initial-value: 0; \}/);
  // …and once a real cloud has flown, the mask and the transition are off for good.
  assert.match(THEME, /#app-tooltip\.dust-driven \{[\s\S]*?mask-image: none;/);
  const tip = readFileSync(new URL('../src/lib/controlTooltip.js', import.meta.url), 'utf8');
  assert.match(tip, /surfaceIn\(t, dustPoint\(el\), \{ ms: TIP_IN_MS \}\)/);
  assert.match(tip, /surfaceOut\(tip, dustPoint\(owner\), \{ ms: TIP_OUT_MS \}\)/);
});

test('the injected in-page modal carries the same grain inline (it can link nothing)', () => {
  // MV3: overlay.js is serialized into the HOST page, so its motion has to be self-contained.
  assert.match(OVERLAY, /const grain = \(d\) => \{/);
  assert.match(OVERLAY, /120 - rate \* d/);
  assert.match(OVERLAY, /stop\(160\)\},\$\{stop\(140\)\},\$\{stop\(125\)\}/);
  assert.match(OVERLAY, /mask-size:\$\{SIZES\}/, 'the cell sizes ride along in every step');
  assert.match(OVERLAY, /const SIZES = '4px 4px,7px 7px,11px 11px';/);
  assert.match(OVERLAY, /grainFrames\('stencilPanelIn'/);
  assert.match(OVERLAY, /grainFrames\('stencilPanelOut'/);
  // …and it disperses on the way out instead of blinking away — idempotently, so every
  // close route (Escape, backdrop, the ✕, the ready timeout) lands on "gone" exactly once.
  assert.match(OVERLAY, /if \(leaving\) return;\s*\n\s*leaving = true;/);
  assert.match(OVERLAY, /wrap\.classList\.add\('leaving'\);/);
  assert.match(OVERLAY, /host\.style\.pointerEvents = 'none';/);
  assert.match(OVERLAY, /setTimeout\(\(\) => host\.remove\(\), LEAVE_MS\);/);
  // A half-formed panel is a surprise, not motion.
  assert.match(OVERLAY, /\.panel\{-webkit-mask-image:none !important;mask-image:none !important;\}/);
});

// ── The surfaces themselves ─────────────────────────────────────────────────
// Every menu/dialog that used to scale now plays the dust, and every one of them still
// aims it at the control it grew from.
test('every icon-anchored surface dusts from — and back into — its own control', () => {
  const src = (rel) => readFileSync(new URL(rel, import.meta.url), 'utf8');
  const cases = [
    // [file, what opens it, the point it is aimed at]
    ['../src/lib/actionMenu.js', 'surfaceIn(menuEl, openOrigin);', 'surfaceOut(menuEl, openOrigin);'],
    ['../src/lib/actionMenu.js', 'surfaceIn(fly, centerOf(head));', 'surfaceOut(fly, centerOf(head));'],
    // customSelect hands both halves to showMenu/hideMenu, which aim at the trigger too.
    ['../src/lib/dropdownMenu.js', 'surfaceIn(menu, menuDustPoint(trigger), { ms: MENU_IN_MS })',
                                   'surfaceOut(menu, menu.hidden ? null : menuDustPoint(menu.__ddTrigger), { ms: MENU_OUT_MS })'],
    ['../src/lib/chatMsgMenu.js', 'surfaceIn(el, openOrigin);', 'surfaceOut(el, openOrigin);'],
    ['../src/popup/dialogShell.js', 'surfaceIn(box, origin', 'surfaceOut(box, origin);'],
    ['../src/options/options.js', 'surfaceIn(box, origin);', 'surfaceOut(box, origin);'],
    // The Main-theme picker drives its own open/close, so it borrows the SAME caret point
    // showMenu aims at — its trigger is a full-width field, and the centre put the list's
    // motes in the middle of the label rather than at the arrow that was pressed.
    ['../src/options/options.js', 'surfaceIn(menu, menuDustPoint(trigger));', 'surfaceOut(menu, menuDustPoint(trigger));'],
    // The chat composer's "…" — the last one still hard-cutting on both edges.
    ['../src/popup/assistant.js', 'surfaceIn(moreMenu, centerOf(moreBtn), { ms: SURFACE_MENU_IN_MS })',
                                  'surfaceOut(moreMenu, centerOf(moreBtn), { ms: SURFACE_MENU_OUT_MS })'],
  ];
  for (const [rel, opens, closes] of cases) {
    const s = src(rel);
    assert.ok(s.includes(opens), `${rel} forms from ${opens}`);
    assert.ok(s.includes(closes), `${rel} comes apart into ${closes}`);
  }
  // The logo's drag menu names the MARK as its point, not the menu's top-left corner.
  assert.match(src('../src/lib/logoDragMenu.js'),
    /placeMenu\(r\.left, r\.bottom \+ 6, \{ x: r\.left \+ r\.width \/ 2, y: r\.top \+ r\.height \/ 2 \}\)/);
});

// `hidden` is display:none, so there is nothing left to copy once it is set — the order
// around each flight is the whole contract for this one.
test('the composer "…" is unhidden before it forms, and hidden right after it leaves', () => {
  const src = readFileSync(new URL('../src/popup/assistant.js', import.meta.url), 'utf8');
  const s = src.slice(src.indexOf('const setMoreOpen = (on) =>'));
  assert.ok(s.indexOf('if (on) moreMenu.hidden = false;') < s.indexOf('surfaceIn(moreMenu'));
  assert.ok(s.indexOf('surfaceOut(moreMenu') < s.indexOf('if (!on) moreMenu.hidden = true;'));
  // Re-closing a closed menu (Escape with nothing up) must not raise a second cloud.
  assert.match(s, /if \(on === !moreMenu\.hidden\) return;/);
  // Every close path still goes through it.
  assert.match(src, /const closeMore = \(\) => setMoreOpen\(false\);/);
});

test('a close is SYNCHRONOUS: the motes are the surface leaving, nothing waits on them', () => {
  const menu = readFileSync(new URL('../src/lib/actionMenu.js', import.meta.url), 'utf8');
  const close = menu.slice(menu.indexOf('const close = () =>'));
  // Dusted while it is still on screen and measurable, hidden on the very same frame —
  // which is what lets a burst of open/close land on the true state.
  assert.ok(close.indexOf('surfaceOut(menuEl, openOrigin);') < close.indexOf('menuEl.hidden = true;'));
  const dlg = readFileSync(new URL('../src/popup/dialogShell.js', import.meta.url), 'utf8');
  assert.ok(dlg.indexOf('surfaceOut(box, origin);') < dlg.indexOf('back.remove();'));
});

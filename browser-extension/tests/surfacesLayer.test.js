// The mote layer itself (src/lib/motion.js surfaceIn / surfaceOut / settleSurface), driven over a
// hand-rolled document: one cloud on <body>, inert, aimed at the icon, and reaped on its own timers.
import test from 'node:test';
import assert from 'node:assert';
import { animationsCss, themeCss } from './helpers/sources.js';

import {
  cancelDust, settleSurface, surfaceIn, surfaceOut, disintegrate,
  SURFACE_IN_MS, SURFACE_OUT_MS,
  SURFACE_DRIVEN_CLASS, SURFACE_FORMING_CLASS, SURFACE_LEAVING_CLASS,
} from '../src/lib/motion.js';
import { PAINT_STOPS } from '../src/lib/dust/dustCloud.js';

const ANIMS = animationsCss();
const THEME = themeCss();

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

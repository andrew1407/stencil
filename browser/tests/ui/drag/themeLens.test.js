// The theme lens (js/ui/drag/themeLens.js): built once at the drag's start — an inert, hidden host
// whose shadow tree adopts the page's sheets and holds the copy — then moved by its clip and its
// rim's offset alone. A preview only: however the drag ends the lens closes and the theme stays,
// and a plain click is still the switch's own. Under the webcore skin, or over a swap in flight,
// no lens opens.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from '../../helpers/dom.js';
import { node, SheetStandIn, pageDoc } from '../../helpers/lensDomRig.js';

globalThis.CSSStyleSheet = SheetStandIn;
globalThis.getComputedStyle = () => ({});
const {
  LENS_RADIUS_PX, LENS_GROW_MS, lensRadiusAt, closingRadiusAt, LENS_CLASS, RIM_CLASS, otherTheme, lensClip, rimTransform, openThemeLens, themeLensHooks,
} = await import('../../../js/ui/drag/themeLens.js');
const { NO_COPY_ATTR, COPY_CLASS } = await import('../../../js/ui/accent/themeCopy.js');
const { createIconDrag } = await import('../../../js/ui/drag/iconDrag.js');
const { THEME_INSTANT_CLASS, THEME_SWAP_CLASS } = await import('../../../js/ui/motion.js');

test('the other theme, the circle and the rim\'s offset', () => {
  assert.deepEqual([otherTheme('dark'), otherTheme('light')], ['light', 'dark']);
  assert.equal(lensClip(300, 200), `circle(${LENS_RADIUS_PX}px at 300px 200px)`);
  assert.equal(rimTransform(300, 200), `translate(${300 - LENS_RADIUS_PX}px, ${200 - LENS_RADIUS_PX}px) scale(1)`);
  assert.equal(rimTransform(300, 200, LENS_RADIUS_PX / 2), `translate(${300 - LENS_RADIUS_PX}px, ${200 - LENS_RADIUS_PX}px) scale(0.5)`);
});

test('opened once: an inert, hidden host over a shadow copy in the other theme, ringed by its rim', () => {
  const sheet = { cssRules: [{ cssText: ':root { --a: 1 }' }], media: { mediaText: '' } };
  const doc = pageDoc({ kids: [node('div', { id: 'root' })], html: { 'data-theme': 'dark' }, sheets: [sheet] });
  globalThis.getComputedStyle = (el) => (el === doc.body ? ['--text-label', 'color'] : {});
  const lens = openThemeLens(300, 200, { theme: 'light', doc, still: true });
  const { host, rim } = lens;
  assert.equal(host.style['--text-label'], 'initial', 'the copy inherits nothing the dark page hands down');
  assert.deepEqual(doc.body.children.slice(-2), [host, rim]);
  assert.ok(host.classes.has(LENS_CLASS) && rim.classes.has(RIM_CLASS));
  assert.ok(host.hasAttribute(NO_COPY_ATTR) && rim.hasAttribute(NO_COPY_ATTR), 'a later copy leaves them out');
  assert.equal(host.inert, true, 'nothing in it takes focus or a pointer');
  assert.equal(host.getAttribute('aria-hidden'), 'true');
  assert.deepEqual(host.shadowInit, { mode: 'open' });
  assert.equal(host.shadowRoot.adoptedStyleSheets[0].text, `.${COPY_CLASS} { --a: 1 }`);
  const copyRoot = host.shadowRoot.children[0];
  assert.equal(copyRoot.getAttribute('data-theme'), 'light');
  assert.equal(rim.style.width, `${2 * LENS_RADIUS_PX}px`);
  assert.equal(host.style.clipPath, lensClip(300, 200));
  lens.move(410, 90);
  assert.equal(host.style.clipPath, lensClip(410, 90));
  assert.equal(rim.style.transform, rimTransform(410, 90));
  assert.equal(host.shadowRoot.children[0], copyRoot, 'a move rebuilds nothing');
  const next = openThemeLens(10, 10, { theme: 'light', doc });
  assert.ok(!doc.body.children.includes(host) && !doc.body.children.includes(rim), 'one lens at a time');
  next.close();
  assert.ok(!doc.body.children.includes(next.host));
});

test('the circle opens from a point to its radius, eased, and stops growing once full', () => {
  assert.equal(lensRadiusAt(0), 0);
  assert.ok(lensRadiusAt(LENS_GROW_MS / 2) > LENS_RADIUS_PX / 2, 'eased out: past half by mid-way');
  assert.equal(lensRadiusAt(LENS_GROW_MS), LENS_RADIUS_PX);
  const doc = pageDoc({ kids: [node('div')] });
  globalThis.getComputedStyle = () => ({});
  const frames = [];
  let t = 0;
  const lens = openThemeLens(50, 60, { theme: 'light', doc, still: false, raf: (fn) => frames.push(fn), now: () => t });
  assert.equal(lens.radius, 0, 'nothing shows at the first instant');
  assert.equal(lens.host.style.clipPath, lensClip(50, 60, 0));
  t = LENS_GROW_MS / 2;
  frames.shift()();
  assert.ok(lens.radius > 0 && lens.radius < LENS_RADIUS_PX);
  lens.move(70, 80);
  assert.equal(lens.host.style.clipPath, lensClip(70, 80, lens.radius), 'a move keeps the radius it has grown to');
  t = LENS_GROW_MS;
  while (frames.length) frames.shift()();
  assert.equal(lens.radius, LENS_RADIUS_PX);
  assert.equal(lens.rim.style.transform, rimTransform(70, 80));
  lens.close();
  assert.ok(doc.body.children.includes(lens.host), 'closing, the circle is still there…');
  t += LENS_GROW_MS / 2;
  frames.shift()();
  assert.ok(lens.radius > 0 && lens.radius < LENS_RADIUS_PX, '…shrinking');
  t += LENS_GROW_MS;
  while (frames.length) frames.shift()();
  assert.equal(lens.radius, 0);
  assert.ok(!doc.body.children.includes(lens.host) && !doc.body.children.includes(lens.rim), '…then gone');
  assert.equal(closingRadiusAt(0, 60), 60);
  assert.equal(closingRadiusAt(LENS_GROW_MS, 60), 0);
});

// A fake lens and an app whose setTheme records any call, over a stand-in page root.
const rig = ({ skin = null } = {}) => {
  const opened = [];
  const open = (x, y, opts) => {
    const lens = { at: [x, y], opts, moves: [], closed: 0, move(px, py) { lens.moves.push([px, py]); }, close() { lens.closed++; } };
    opened.push(lens);
    return lens;
  };
  const root = createStubElement('html');
  if (skin) root.setAttribute('data-skin', skin);
  const calls = [];
  const picture = createStubElement('canvas');
  const app = { theme: 'dark', renderer: { layers: () => [picture] }, accents: { setTheme: (...a) => calls.push(a) } };
  return { opened, calls, root, picture, app, hooks: themeLensHooks(app, { open, doc: { documentElement: root } }) };
};

test('the start opens the lens in the other theme on the pointer; moves move it', () => {
  const { opened, picture, hooks } = rig();
  hooks.start({ x: 40, y: 50 });
  assert.equal(opened.length, 1);
  assert.deepEqual(opened[0].at, [40, 50]);
  assert.equal(opened[0].opts.theme, 'light');
  assert.equal(opened[0].opts.isPicture(picture), true, 'the stage\'s layers are the picture');
  assert.equal(opened[0].opts.isPicture(createStubElement('canvas')), false);
  hooks.move({ x: 60, y: 70 });
  assert.deepEqual(opened[0].moves, [[60, 70]]);
});

test('a preview only: a drop anywhere, or a cancel, closes the lens and switches nothing', () => {
  for (const end of ['drop', 'cancel']) {
    const { opened, calls, hooks } = rig();
    hooks.start({ x: 1, y: 1 });
    hooks.move({ x: 300, y: 200 });
    hooks[end]({ x: 300, y: 200 });
    assert.equal(opened[0].closed, 1, end);
    assert.deepEqual(calls, [], `${end}: the theme stays`);
  }
});

test('through the drag machine: released off the switch or back on it, the theme stays', () => {
  const { opened, calls, hooks } = rig();
  const toggle = { left: 0, top: 0, right: 32, bottom: 32 };
  const m = createIconDrag({ ...hooks, originRect: () => toggle });
  m.press(16, 16, {});
  m.move(500, 400);
  assert.equal(m.release(500, 400), true, 'the click the release would make is the drag\'s');
  m.press(16, 16, {});
  m.move(500, 400);
  m.move(18, 14);
  assert.equal(m.release(18, 14), true);
  assert.deepEqual(opened.map((l) => l.closed), [1, 1]);
  assert.deepEqual(calls, []);
  m.press(16, 16, {});
  assert.equal(m.release(17, 16), false, 'a plain click stays the switch\'s own toggle');
  assert.equal(opened.length, 2, 'and opens no lens');
});

test('under the webcore skin, or over a swap still in flight, no lens opens and nothing switches', () => {
  const skinned = rig({ skin: 'webcore' });
  assert.notEqual(skinned.hooks.start({ x: 1, y: 1 }), false, 'the drag goes on without one');
  skinned.hooks.move({ x: 5, y: 5 });
  skinned.hooks.drop({ x: 5, y: 5 });
  assert.deepEqual([skinned.opened, skinned.calls], [[], []]);
  for (const flight of [THEME_INSTANT_CLASS, THEME_SWAP_CLASS]) {
    const { opened, calls, root, hooks } = rig();
    root.classList.add(flight);
    hooks.start({ x: 1, y: 1 });
    hooks.drop({ x: 5, y: 5 });
    assert.deepEqual([opened, calls], [[], []], flight);
  }
});

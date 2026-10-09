// The context menu's dust point (js/ui/contextMenu/contextMenu.js surfaceIn / surfaceOut): the real
// menu, mounted by helpers/ctxMenuMountRig.js with motion on, opened by a right-click on the canvas
// and closed by Escape or a press outside. Split from llm/surfaceDust-origins.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mountContextMenu } from '../../helpers/ctxMenuMountRig.js';
import { rect, cloudAim, near, cloudKind } from '../../helpers/dustRig.js';
import { ANIMATIONS_CSS } from '../../helpers/css.js';
import { SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from '../../../js/ui/motion.js';
import { setMotionPrefs } from '../../../js/ui/motion/motionPrefs.js';

const ZERO = rect(0, 0, 0, 0);
// The rig reduces motion; this page lets it move. The menu is display:none while closed, so it
// has a box only while it is open.
const mountMoving = async (t) => {
  const m = await mountContextMenu(t);
  const reduced = { on: false };
  globalThis.matchMedia = (q) => ({ matches: reduced.on && q.includes('reduced-motion') });
  setMotionPrefs({ mode: 'particles' });
  const { menu, isOpen } = m;
  menu.getBoundingClientRect = () => (isOpen()
    ? rect(parseFloat(menu.style.left), parseFloat(menu.style.top), 160, 120) : ZERO);
  const pressOutside = () => m.doc.listeners('mousedown').forEach((l) => l.fn({ target: m.doc.body }));
  return { ...m, reduced, pressOutside };
};

test('the context menu forms out of the very click it was opened at, and pours back into it', async (t) => {
  const m = await mountMoving(t);
  m.open(300, 200);
  assert.equal(cloudKind(m.menu), 'dust-forming');
  assert.ok(near(cloudAim(m.menu.__dustHost), { x: 300, y: 200 }), 'out of the click');
  assert.equal(m.menu.style['--dust-ms'], `${SURFACE_MENU_IN_MS}ms`);
  // A second right-click re-opens it where that one landed, and the close follows.
  m.open(620, 90);
  assert.ok(near(cloudAim(m.menu.__dustHost), { x: 620, y: 90 }), 'out of the newer click');
  m.doc.key('Escape');
  // A closed menu has no box, so a cloud at all proves it was measured while still open.
  assert.ok(!m.isOpen(), 'Escape closes it');
  assert.equal(cloudKind(m.menu), 'dust-leaving', 'measured before ctx-open came off');
  assert.ok(near(cloudAim(m.menu.__dustHost), { x: 620, y: 90 }), 'back into the point it grew out of');
  assert.equal(m.menu.style['--dust-ms'], `${SURFACE_MENU_OUT_MS}ms`);
});

test('closing a menu that is not open plays nothing: closeMenu is also the idle teardown', async (t) => {
  const m = await mountMoving(t);
  const clouds = () => m.doc.body.children.filter((c) => c.__cloud).length;
  m.pressOutside();
  assert.equal(clouds(), 0, 'a press anywhere on an idle page raises no cloud');
  m.open(300, 200);
  m.pressOutside();
  assert.equal(cloudKind(m.menu), 'dust-leaving', 'the press that closes it plays the close');
  const flying = clouds();
  m.pressOutside();
  assert.ok(clouds() <= flying && cloudKind(m.menu) !== 'dust-leaving', 'the next press has nothing to close');
});

test('with motion off, or the OS asking for less, the menu opens and closes without a cloud', async (t) => {
  const m = await mountMoving(t);
  for (const [what, set, unset] of [
    ['motion none', () => setMotionPrefs({ mode: 'none' }), () => setMotionPrefs({ mode: 'particles' })],
    ['prefers-reduced-motion', () => { m.reduced.on = true; }, () => { m.reduced.on = false; }],
  ]) {
    set();
    m.open(300, 200);
    assert.ok(m.isOpen() && cloudKind(m.menu) === null, `${what}: opens with no cloud`);
    m.doc.key('Escape');
    assert.ok(!m.isOpen() && cloudKind(m.menu) === null, `${what}: closes with no cloud`);
    unset();
  }
});

// The pop it replaces set a transform, which would have made the menu the containing block for
// its position:fixed flyouts. The dust drives opacity only.
test('surfaceForm animates opacity alone', () => {
  const css = ANIMATIONS_CSS;
  const form = css.match(/@keyframes surfaceForm\s*\{([\s\S]*?\})\s*\}/)?.[1] ?? '';
  assert.match(form, /opacity/, 'the surfaceForm keyframes are gone');
  assert.doesNotMatch(form, /transform/, 'surfaceForm animates opacity alone');
  assert.match(css, /\.dust-driven\.surface-forming \{ animation: surfaceForm /, 'the forming class plays it');
});

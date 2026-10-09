// The two folding surfaces (toolbar rows, points panel): foldBox reads the box they are about
// to reach, with the transitions off, and each fold's cloud is driven through its real click.
// Split from surfaceDust.test.js.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { createStubElement } from '../helpers/dom.js';
import { installDustDom, rect, boxEl, cloudAim, near } from '../helpers/dustRig.js';
import { ANIMATIONS_CSS } from '../helpers/css.js';

let reduced = false;
globalThis.MutationObserver = class { observe() {} disconnect() {} };
globalThis.customElements = { define: () => {}, get: () => undefined };
globalThis.HTMLElement = class {};
const dust = installDustDom({
  reduced: () => reduced, docOpts: { autoCreateById: true },
  globals: { window: { addEventListener: () => {}, removeEventListener: () => {}, dispatchEvent: () => {} } },
});
const { doc } = dust;
doc.els.set('panel-resizer', null);
doc.querySelectorAll = (sel) => (sel === '.disintegrate-host' ? dust.clouds() : []);

const {
  foldBox, foldDust, FOLD_INSTANT_CLASS, FOLD_DUST_OUT_MS, SURFACE_IN_MS, SURFACE_OUT_MS,
  CONTROLS_DUST_IN_MS, CONTROLS_DUST_OUT_MS,
  SURFACE_FORMING_CLASS, SURFACE_LEAVING_CLASS, BELOW_CHAT_CLASS, surfaceIn, surfaceOut,
  settleSurface, disintegrate, dockAwayPoint,
} = await import('../../js/ui/motion.js');
const { setMotionPrefs } = await import('../../js/ui/motion/motionPrefs.js');
const { StencilToolbar } = await import('../../js/ui/toolbar/toolbar.js');
const { StencilMainContent } = await import('../../js/ui/panel/mainContent.js');
const { showSelectionPanel, hideSelectionPanels } = await import('../../js/ui/panel/selectionPanel.js');
const { PANEL_DUST_IN_MS, PANEL_DUST_OUT_MS } = await import('../../js/ui/panel/coordFold.js');

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const animCss = ANIMATIONS_CSS;
const hostBox = (h) => [h.style.left, h.style.top, h.style.width, h.style.height];

// A registered element whose box is `open` unless its scope wears `cls`; each read notes the
// scope's state, so a case can tell WHEN the box was measured.
const folding = (id, cls, open, scope = null) => {
  const reads = [];
  let folded = false;   // the fold has flipped the class on at least once
  const el = createStubElement('div', {
    getBoundingClientRect: () => {
      const s = scope || el;
      reads.push({ shut: s.classes.has(cls), instant: s.classes.has(FOLD_INSTANT_CLASS),
                   folding: s.classes.has('coord-folding'), folded });
      return s.classes.has(cls) ? rect(open.left, open.top, 0, 0) : open;
    },
  });
  const s = scope || el;
  const toggle = s.classList.toggle;
  s.classList.toggle = (c, on) => { if (c === cls && on) folded = true; return toggle(c, on); };
  doc.register(id, el);
  return { el, reads };
};

// The two folding surfaces have no opener icon and CSS owns their whole reveal, so at the moment
// the toggle flips they are still at the box they are LEAVING: foldBox supplies the box.

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
  dust.reset();
  const box = rect(40, 60, 600, 120);
  const collapsed = boxEl(rect(40, 60, 600, 0));
  assert.equal(surfaceIn(collapsed, { x: 0, y: 0 }), false, 'measured live, a collapsed fold declines');
  assert.equal(surfaceIn(collapsed, { x: 0, y: 0 }, { box }), true, 'handed its box, it flies');
  assert.deepEqual(hostBox(collapsed.__dustHost), ['40px', '60px', '600px', '120px']);
  assert.equal(collapsed.style['--dust-ms'], `${SURFACE_IN_MS}ms`, 'surfaceIn keeps its own clock');
  settleSurface(collapsed);
  assert.equal(surfaceOut(collapsed, { x: 0, y: 0 }, { box }), true);
  assert.equal(collapsed.style['--dust-ms'], `${SURFACE_OUT_MS}ms`, '…and surfaceOut its own');
  const bare = boxEl(rect(0, 0, 0, 0));
  assert.equal(disintegrate(bare, { box }), true, 'disintegrate itself builds over the handed box');
  assert.deepEqual(hostBox(bare.__dustHost), ['40px', '60px', '600px', '120px']);
  // The suppression class is real CSS, on both folds and their fading children.
  const instant = animCss.slice(animCss.indexOf('#controls-body.fold-instant'));
  assert.match(instant.slice(0, instant.indexOf('}')), /transition: none !important/);
  for (const sel of ['#controls-body.fold-instant > *', '.coordinates-panel.fold-instant',
                     '.coordinates-panel.fold-instant #coord-body'])
    assert.ok(animCss.includes(sel), `${sel} is suppressed for the read`);
});

test('the tool rows dust up past the top edge, measured before the class flips', () => {
  // Both folds ride ONE shared ritual (foldDust): measure the SHOWN box, then let the caller
  // fold, aim past the dock edge, and give the collapse the fold's slower exit clock.
  dust.reset();
  setMotionPrefs({ mode: 'particles' });
  const open = rect(0, 120, 900, 160);
  const { el: body, reads } = folding('controls-body', 'hidden', open);
  const bar = Object.create(StencilToolbar.prototype);
  Object.assign(bar, { querySelector: () => null, querySelectorAll: () => [] });
  bar.wire({});
  doc.getElementById('toggle-controls').dispatch('click');
  assert.ok(body.classes.has('hidden'), 'the rows fold');
  assert.ok(reads.length && !reads[0].shut && reads[0].instant && !reads[0].folded,
    'the box is measured shown, with the transitions off, before the fold starts');
  assert.deepEqual(hostBox(body.__dustHost), ['0px', '120px', '900px', '160px']);
  assert.ok(near(cloudAim(body.__dustHost), dockAwayPoint(open, 'top')), 'aimed past the top edge');
  assert.ok(body.classes.has(SURFACE_LEAVING_CLASS));
  assert.equal(body.style['--dust-ms'], `${CONTROLS_DUST_OUT_MS}ms`, 'the collapse keeps the toolbar\'s exit clock');
  doc.getElementById('toggle-controls').dispatch('click');
  assert.ok(!body.classes.has('hidden') && body.classes.has(SURFACE_FORMING_CLASS), 'and gathers back out of it');
  assert.equal(body.style['--dust-ms'], `${CONTROLS_DUST_IN_MS}ms`);
  // The toolbar's own clocks, 1.3x brisker than the shared ones (user request).
  assert.deepEqual([CONTROLS_DUST_IN_MS, CONTROLS_DUST_OUT_MS],
    [Math.round(SURFACE_IN_MS / 1.3), Math.round(FOLD_DUST_OUT_MS / 1.3)]);
});

test('the points panel pours out past the right edge it collapses towards, on the desktop clocks', () => {
  dust.reset();
  setMotionPrefs({ mode: 'particles' });
  const panel = createStubElement('div', { id: 'coord-panel' });
  doc.register('coord-panel', panel);
  const open = rect(900, 80, 300, 500);
  doc.getElementById('toggle-coord-panel').parentElement = boxEl(rect(900, 80, 300, 28));
  const { reads } = folding('coord-body', 'coord-collapsed', rect(900, 116, 300, 464), panel);
  StencilMainContent.prototype.wire.call(createStubElement('stencil-main-content'), null);
  panel.classes.add('coord-folding');
  doc.getElementById('toggle-coord-panel').dispatch('click');
  assert.ok(panel.classes.has('coord-collapsed'), 'the panel collapses');
  // .coord-folding takes the table out of the layout — it must be off for the read.
  assert.ok(reads.length && reads.every((r) => !r.folding), 'the fold hold is lifted before the box is read');
  assert.ok(!reads[0].shut && reads[0].instant && !reads[0].folded);
  assert.deepEqual(hostBox(panel.__dustHost), ['900px', '80px', '300px', '500px'], 'header and table, as one picture');
  assert.ok(near(cloudAim(panel.__dustHost), dockAwayPoint(open, 'right')), 'aimed past the right edge');
  assert.equal(panel.style['--dust-ms'], `${PANEL_DUST_OUT_MS}ms`);
  doc.getElementById('toggle-coord-panel').dispatch('click');
  assert.ok(!panel.classes.has('coord-collapsed'));
  assert.equal(panel.style['--dust-ms'], `${PANEL_DUST_IN_MS}ms`, 'and gathers back on its own clock');
});

test('neither fold dusts under reduced motion — the box is not even measured', () => {
  // Reduced motion skips the two forced layouts as well as the flight.
  for (const [mode, os] of [['none', false], ['particles', true]]) {
    setMotionPrefs({ mode });
    reduced = os;
    const { el, classes, reads } = foldStub('hidden', rect(0, 0, 900, 160));
    let toggled = 0;
    foldDust(el, el, 'hidden', true, 'top', { toggle: () => { toggled++; classes.delete('hidden'); } });
    assert.equal(reads.length, 0, `${mode}${os ? ' + OS' : ''}: not measured`);
    assert.equal(toggled, 1, 'the fold itself still happens');
    assert.ok(!el.__dustHost);
    reduced = false;
  }
  setMotionPrefs({ mode: 'particles' });
});

// A page surface's cloud stays UNDER a docked chat: with the chat on the top edge, the tool
// rows' away point lands inside the panel, and a cloud above it drew the fold's motes across
// the chat instead of beneath it.
test('the fold and the points bar layer their dust below the chat panel', () => {
  dust.reset();
  setMotionPrefs({ mode: 'particles' });
  assert.equal(BELOW_CHAT_CLASS, 'dust-below-chat');
  for (const hiding of [true, false]) {
    const el = boxEl(rect(0, 0, 400, 100));
    foldDust(el, el, 'hidden', hiding, 'top');
    assert.equal(el.__dustHost.className,
      `disintegrate-host ${hiding ? 'dust-leaving' : 'dust-forming'} ${BELOW_CHAT_CLASS}`);
  }
  const bar = doc.register('selection-panel', boxEl(rect(0, 300, 600, 44)));
  bar.style.display = 'none';
  const app = { pointSize: 4, syncFsSelectionPanel: () => {}, renderLinesList: () => {} };
  showSelectionPanel(app, { color: '#ff0000', thickness: 2 });
  assert.match(bar.__dustHost.className, /dust-forming dust-below-chat$/, 'the points bar gathers below the chat');
  hideSelectionPanels();
  assert.match(bar.__dustHost.className, /dust-leaving dust-below-chat$/, '…and leaves below it');
  const panelZ = Number(/stencil-chat-panel\s*\{[^}]*z-index:\s*(\d+)/.exec(read('../../css/components/chat/panel.css'))[1]);
  const hostZ = Number(/\.disintegrate-host\s*\{[^}]*z-index:\s*(\d+)/.exec(animCss)[1]);
  const belowZ = Number(/\.disintegrate-host\.dust-below-chat\s*\{[^}]*z-index:\s*(\d+)/.exec(animCss)[1]);
  assert.ok(hostZ > panelZ, 'an ordinary cloud (a chat menu) still flies over the chat');
  assert.ok(belowZ < panelZ, 'a page surface cloud sits under the chat panel');
});

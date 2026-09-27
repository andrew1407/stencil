// The Settings section's ghost icon buttons share the toolbar's standard icon box. Their 1px border
// is why the padding is 7/11 rather than the 8/12 the borderless buttons use:
// 16 + 14 + 2 = 32, 16 + 22 + 2 = 40.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { LAYOUT_CSS, COMPONENTS_CSS, ANIMATIONS_CSS } from '../../helpers/css.js';
import { installDom, createStubElement } from '../../helpers/dom.js';
import { wireOpenState } from '../../../js/ui/chat/panel/openState.js';
import { resetViewportScroll, scrollViewportTo } from '../../../js/ui/panel/layoutControls.js';
import { GHOST_MS } from '../../../js/ui/motion.js';
import { mountStorage } from '../../helpers/storageRig.js';

const SHEETS = { 'layout.css': LAYOUT_CSS, 'components.css': COMPONENTS_CSS, 'animations.css': ANIMATIONS_CSS };
const read = (f) => SHEETS[f] ?? readFileSync(new URL(`../../../css/${f}`, import.meta.url), 'utf8');
// The declaration block for a selector, as written.
const ruleFor = (css, selector) => {
  const at = css.indexOf(selector + ' {');
  assert.ok(at >= 0, `rule not found: ${selector}`);
  return css.slice(at, css.indexOf('}', at));
};
const paddingOf = (block) => block.match(/padding:\s*([^;]+);/)?.[1].trim();

// The bordered ghost buttons: Settings' gear/palette/help/fullscreen/incognito, plus the
// theme toggle, which is the same treatment declared in its own file.
const GHOSTS = [
  ['components.css', '#settings-btn, #visuals-btn, #info-btn, #fullscreen-toggle, #incognito-toggle'],
  ['layout.css', '#theme-toggle'],
];

test('the bordered ghost buttons box to the same 40x32 as the other icon buttons', () => {
  for (const [file, selector] of GHOSTS) {
    const pad = paddingOf(ruleFor(read(file), selector));
    assert.equal(pad, '7px 11px',
                 `${selector} padding is ${pad} — with its 1px border that is not the 40x32 box`);
  }
});

test('the ghost buttons all agree with each other', () => {
  const pads = GHOSTS.map(([file, sel]) => paddingOf(ruleFor(read(file), sel)));
  assert.equal(new Set(pads).size, 1, `the ghost rows disagree: ${pads.join(' vs ')}`);
});

// The chat panel's open state over a stub host, its toolbar icon at ICON; a right-click on the
// icon is the compact gesture (tip/popover.js).
const ICON = { left: 900, top: 10, width: 40, height: 32, right: 940, bottom: 42 };
const chatRig = (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const doc = installDom({ autoCreateById: true }, {
    window: { innerWidth: 1280, innerHeight: 800, addEventListener() {}, removeEventListener() {} },
    matchMedia: () => ({ matches: false }),
  });
  t.after(doc.restore);
  const host = createStubElement('stencil-chat-panel', {
    getBoundingClientRect: () => ({ left: 300, top: 100, width: 380, height: 520, right: 680, bottom: 620 }),
  });
  const [openBtn, input] = [createStubElement('button', { getBoundingClientRect: () => ICON }), createStubElement('textarea')];
  const state = wireOpenState({
    host, input, openBtn, resizer: createStubElement('div'), header: createStubElement('div'),
    backdrop: createStubElement('div'), closeBtn: createStubElement('button'),
    panelIsOpen: () => host.classList.contains('chat-open') && !host.classList.contains('chat-closing'),
    refreshStatus: () => {}, invalidatePillRects: () => {},
  });
  return { host, input, ...state, compactGesture: () => openBtn.dispatch('contextmenu', { preventDefault() {} }) };
};
const has = (el, cls) => el.classList.contains(cls);

// The FLOAT chat is its own little window, so it flies out of the toolbar icon and shrinks back into
// it — the modals' own modalFromIcon/modalToIcon; panel.js feeds it the icon→panel delta.
test('the floating chat panel animates from the toolbar icon', (t) => {
  const css = ANIMATIONS_CSS;
  const open = css.match(/stencil-chat-panel\.chat-open\.chat-dock-float\s*\{([^}]*)\}/)?.[1] || '';
  const close = css.match(/stencil-chat-panel\.chat-open\.chat-closing\.chat-dock-float\s*\{([^}]*)\}/)?.[1] || '';
  assert.match(open, /modalFromIcon/, `float open is "${open.trim()}"`);
  assert.match(close, /modalToIcon/, `float close is "${close.trim()}"`);

  const { host, setDock, setOpen } = chatRig(t);
  setDock('float');
  setOpen(true);
  // The keyframes read these four; all of them have to be set, or the flight silently
  // falls back to the keyframes' plain-pop defaults.
  const [x, y, w, h] = ['left', 'top', 'width', 'height'].map((k) => parseFloat(host.style[k]));
  assert.equal(host.style['--modal-dx'], `${Math.round(ICON.left + ICON.width / 2 - (x + w / 2))}px`);
  assert.equal(host.style['--modal-dy'], `${Math.round(ICON.top + ICON.height / 2 - (y + h / 2))}px`);
  assert.equal(Number(host.style['--modal-sx']), ICON.width / w);
  assert.equal(Number(host.style['--modal-sy']), ICON.height / h);
  // The close timer has to outlast the longer float flight, or the panel is torn out of the DOM
  // mid-motion, so docked shares the same 510ms.
  for (const dock of ['float', 'left']) {
    setDock(dock);
    setOpen(true);
    setOpen(false);
    assert.equal(host.style['--dust-ms'], '510ms', `${dock}: the close dust rides the one clock`);
    t.mock.timers.tick(509);
    assert.ok(has(host, 'chat-open'), `${dock}: still on screen 1ms short of the close`);
    t.mock.timers.tick(1);
    assert.ok(!has(host, 'chat-open'), `${dock}: gone once the 510ms close is over`);
  }
});

// The clear animation runs only when an image was actually there, and a float → compact swap waits
// for the close animation before playing the compact panel's own entrance.
test('the float → mini chat swap waits for the close animation', (t) => {
  const { host, input, setDock, setOpen, chatDock, compactGesture } = chatRig(t);
  const log = [];
  for (const m of ['add', 'remove', 'toggle']) {
    const real = host.classList[m];
    host.classList[m] = (...a) => { if (a[0] === 'chat-open' || a[1] === 'chat-open') log.push(m); return real(...a); };
  }
  setDock('float');
  setOpen(true);
  Object.assign(host, { innerHTML: '<div id="chat-transcript">kept</div>' });
  input.value = 'a draft';
  // An open panel is closed FIRST, with the sequel queued behind it.
  log.length = 0;
  compactGesture();
  assert.ok(has(host, 'chat-closing') && !chatDock.isCompact(), 'the outgoing shape plays its exit first');
  t.mock.timers.tick(509);
  assert.ok(!chatDock.isCompact(), 'no compact panel while the float one is still closing');
  t.mock.timers.tick(1);
  assert.ok(chatDock.isCompact() && has(host, 'chat-open') && !has(host, 'chat-closing'));
  // The sequel fires from the close timer itself, after .chat-open comes off — one panel on screen at a time.
  assert.deepEqual(log, ['remove', 'toggle'], 'chat-open comes off before the compact shape takes it');
  // Same host, same controller: the swap only re-shapes, it never rebuilds the panel or the conversation.
  assert.equal(host.innerHTML, '<div id="chat-transcript">kept</div>', 'the shape swap must not tear down the conversation');
  assert.equal(input.value, 'a draft');
  // Mid-close (the eager click) is ridden out, not cancelled or restarted.
  setOpen(false);
  t.mock.timers.tick(510);
  setOpen(true);
  setOpen(false);
  t.mock.timers.tick(200);
  compactGesture();
  assert.ok(has(host, 'chat-closing'), 'a compact gesture cancels the in-flight close again');
  t.mock.timers.tick(310);
  assert.ok(chatDock.isCompact() && has(host, 'chat-open'), 'the compact panel follows the ORIGINAL close');
});

// A real Storage over the stub page (helpers/storageRig.js). Both halves of the clear motion — the
// dust and the empty-state hold — ride ghostOut's own verdict: with no dust, a hold only blanks it.
test('newTemporary only animates when there was an image to clear', (t) => {
  const r = mountStorage(t);
  const holding = () => r.viewport.classList.contains('canvas-clearing');
  r.storage.newTemporary();
  assert.ok(holding(), 'the emptied editor waits under the dust');
  assert.equal(r.stages().length, 1, 'the dust plays over the viewport');
  // Snapshotted from the picture and its lines, before the picture is cleared.
  const cleared = r.ops.findIndex(([el, k]) => el === r.canvas && k === 'clearRect');
  const layers = [r.canvas, r.overlay];
  const snap = r.ops.filter(([, k, src], i) => k === 'drawImage' && i < cleared && layers.includes(src));
  assert.deepEqual(snap.map(([, , src]) => src), layers, 'the snapshot is the picture and its lines');
  t.mock.timers.tick(GHOST_MS);
  assert.ok(!holding(), 'handed back once the dust has flown');
  for (const [what, arm] of [['boot: nothing was on screen', { image: null }],
                             ['reduced motion: no dust to wait on', { reduced: true }]]) {
    r.rearm(arm);
    const stages = r.stages().length;
    r.storage.newTemporary();
    assert.ok(!holding() && r.stages().length === stages, what);
  }
});

// Clearing resets the canvas backing store and the viewport scroll: the idle "+ Blank image" card is
// position:absolute inset:0 in that same box and renders at the stale offset (user report).
test('newTemporary resets the canvas size/zoom and the viewport scroll, not just the pixels', (t) => {
  const { storage, app, canvas, viewport, rearm } = mountStorage(t);
  storage.newTemporary();
  assert.deepEqual([canvas.width, canvas.height], [0, 0], 'the backing store keeps its old (zoomed) footprint');
  assert.deepEqual([canvas.style.width, canvas.style.height], ['', ''], 'a stale inline CSS size survives the clear');
  assert.deepEqual([app.scale, app.renderedScale, app.zoomPan.value], [1, null, 100], 'the zoom level is never reset on clear');
  assert.deepEqual([viewport.scrollLeft, viewport.scrollTop], [0, 0], 'the viewport scroll position is never reset on clear');
  // An image-less stored layout empties the editor through the same collapse.
  rearm();
  storage.loadPayloadIntoApp({ layout: {} });
  assert.equal(app.image, null);
  assert.deepEqual([canvas.width, canvas.height, canvas.style.width, canvas.style.height], [0, 0, '', ''],
    'an image-less layout leaves the canvas at the old size');
});

// …and that helper (ui/panel/layoutControls.js) is what actually puts it back to the corner.
test('the viewport scroll reset puts the canvas back at its top-left corner', (t) => {
  const doc = installDom();
  t.after(doc.restore);
  const vp = doc.register('canvas-viewport', { scrollLeft: 240, scrollTop: 180 });
  resetViewportScroll();
  assert.deepEqual([vp.scrollLeft, vp.scrollTop], [0, 0]);
  scrollViewportTo(30, undefined);
  assert.deepEqual([vp.scrollLeft, vp.scrollTop], [30, 0], 'a missing offset is the edge, never NaN');
});

// The collapsed rail's chevron is an explicit flex square: as a `display: block` button its height
// came from the 16px font's line box plus the inline SVG's baseline gap — 24x25, glyph riding high.
test('the points-panel chevron is one centred square in both states', () => {
  const css = LAYOUT_CSS;
  const base = css.match(/\n#toggle-coord-panel \{([^}]*)\}/)?.[1] || '';
  assert.ok(base, 'the chevron rule is gone');
  // The box has to come from the RULE, not from font metrics — a 16px glyph in a text
  // line box gave a 30x22 pill open and a 28x28 square collapsed: same control, two shapes.
  assert.match(base, /display:\s*flex/, `still laid out as ${base.match(/display:\s*\w+/)?.[0]}`);
  assert.match(base, /align-items:\s*center/);
  assert.match(base, /justify-content:\s*center/);
  assert.match(base, /padding:\s*0/);
  assert.match(base, /line-height:\s*1\b/);
  const w = base.match(/width:\s*(\d+)px/)?.[1];
  const h = base.match(/height:\s*(\d+)px/)?.[1];
  assert.ok(w && h, 'the chevron has no explicit box, so its height follows the font');
  assert.equal(w, h, `the chevron is ${w}x${h}, not square`);
  // Collapsing may drop the outline, but it must NOT re-open the box.
  const rail = css.match(/\.coordinates-panel\.coord-collapsed #toggle-coord-panel \{([^}]*)\}/)?.[1] || '';
  assert.ok(rail, 'the collapsed rule is gone');
  for (const prop of ['width', 'height', 'padding', 'font-size', 'display'])
    assert.ok(!new RegExp(`${prop}\\s*:`).test(rail),
              `the collapsed rule re-declares ${prop}, so the two states can drift apart`);
});

// Scroll reveal, the drop landing and the FLIP stretch (js/ui/motion.js), plus the
// animations.css rules that drive them — pinned against a stub DOM, like chat-markup.test.js.
import test from 'node:test';
import assert from 'node:assert';
import {
  observeReveal, flashLanding, flipTransform, revealDissolve, revealGrain, FLIP_MS, FLIP_EASING,
  FLIP_ACTIVE_CLASS,
} from '../../js/ui/motion.js';
import { COMPONENTS_CSS, ANIMATIONS_CSS } from '../helpers/css.js';
import { box, el } from '../helpers/motionRig.js';

// Dissolve tracks ONLY the share of a row the scroller is already clipping —
// the grain is finer than a glyph's strokes, so it must never cost legibility.
const H = 800;

test('a row you can see in FULL is never dissolved at all', () => {
  assert.equal(revealDissolve(0, 60, H), 0, 'flush with the top edge');
  assert.equal(revealDissolve(300, 400, H), 0, 'mid-scroller');
  assert.equal(revealDissolve(H - 50, H, H), 0, 'flush with the bottom edge');
  assert.equal(revealDissolve(0, H, H), 0, 'exactly filling the scroller');
});

test('dissolve equals the clipped share, at either edge', () => {
  assert.equal(revealDissolve(-50, 50, H), 0.5, 'half cut off the top');
  assert.equal(revealDissolve(H - 50, H + 50, H), 0.5, 'half cut off the bottom');
  assert.ok(Math.abs(revealDissolve(-75, 25, H) - 0.75) < 1e-9, 'three quarters gone');
});

test('a row fully out of view is fully dissolved, both ways', () => {
  assert.equal(revealDissolve(-200, -50, H), 1);
  assert.equal(revealDissolve(H + 20, H + 120, H), 1);
});

test('a row taller than the scroller is only dissolved by what hangs off', () => {
  // Half of a 1600px row is on screen, so half of it is dissolved.
  assert.equal(revealDissolve(-800, 800, H), 0.5);
});

// A message taller than the scroller is clipped at any scroll, so it is never speckled:
// legibility outranks the decoration.
test('a row taller than the scroller gets no grain while it fills the view', () => {
  assert.equal(revealGrain(-800, 800, H), 0, 'a 1600px row filling the scroller');
  assert.equal(revealGrain(0, 1600, H), 0, 'its top edge in view, the rest below');
  assert.ok(revealDissolve(-800, 800, H) > 0, 'the soft edge still tracks the clipping');
});

test('grain equals the clipped share for rows that fit', () => {
  assert.equal(revealGrain(300, 400, H), 0);
  assert.equal(revealGrain(-50, 50, H), 0.5);
  assert.equal(revealGrain(H - 50, H + 50, H), 0.5);
});

test('a tall row grains only as it leaves the view', () => {
  assert.equal(revealGrain(-1400, 200, H), 0.75, '200 of a possible 800px still showing');
  assert.equal(revealGrain(-1700, -100, H), 1, 'gone');
  assert.equal(revealGrain(0, 1600, 0), 0, 'a zero-height scroller is not a guess');
});

test('degenerate inputs never dissolve anything', () => {
  assert.equal(revealDissolve(0, 50, 0), 0, 'a zero-height scroller is not a guess');
  assert.equal(revealDissolve(50, 50, H), 0, 'an empty row');
});

test('observeReveal is inert without requestAnimationFrame', () => {
  const prior = globalThis.requestAnimationFrame;
  delete globalThis.requestAnimationFrame;
  try {
    const stop = observeReveal({ addEventListener() {} }, '.row');
    assert.equal(typeof stop, 'function');
    stop();
  } finally { globalThis.requestAnimationFrame = prior; }
});

test('flashLanding is restart-safe and clears itself', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const node = el();
  flashLanding(node, 'drop-landing', 700);
  assert.ok(node.has('drop-landing'));
  // A second drop before the first expires replays it rather than doing nothing.
  flashLanding(node, 'drop-landing', 700);
  assert.ok(node.has('drop-landing'));
  t.mock.timers.tick(699);
  assert.ok(node.has('drop-landing'), 'the first drop\'s timer was cancelled, not left to fire early');
  t.mock.timers.tick(2);
  assert.ok(!node.has('drop-landing'));
});

// The canvas viewport can run two clouds at once (clearing, assembling), so each owns its
// own removal timer.
test('flashLanding tracks a timer per class, not per element', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const node = el();
  flashLanding(node, 'canvas-clearing', 1100);
  t.mock.timers.tick(200);
  flashLanding(node, 'canvas-assembling', 1100);   // must NOT cancel the first removal
  assert.ok(node.has('canvas-clearing') && node.has('canvas-assembling'));
  t.mock.timers.tick(901);
  assert.ok(!node.has('canvas-clearing'), 'the first class still expires on its own clock');
  assert.ok(node.has('canvas-assembling'), 'while the second is still running');
  t.mock.timers.tick(200);
  assert.ok(!node.has('canvas-assembling'), 'and then it goes too');
});

test('flashLanding ignores a missing node', () => {
  assert.doesNotThrow(() => { flashLanding(null); flashLanding(undefined); });
});

// ── FLIP (the fullscreen stretch / minimise) ────────────────────────────────
test('flipTransform inverts the new box back onto the old one', () => {
  // Entering fullscreen: an 800x400 viewport at (40, 120) becomes the whole window.
  const t = flipTransform(box(40, 120, 800, 400), box(0, 0, 1600, 800));
  assert.equal(t, 'translate(40px, 120px) scale(0.5, 0.5)',
    'the fullscreen box starts drawn exactly where the old one was');
});

test('flipTransform runs the other way for the minimise back to the canvas', () => {
  const t = flipTransform(box(0, 0, 1600, 800), box(40, 120, 800, 400));
  assert.equal(t, 'translate(-40px, -120px) scale(2, 2)',
    'the restored box starts drawn at full-window size and shrinks in');
});

test('flipTransform declines when there is nothing to play', () => {
  assert.equal(flipTransform(box(0, 0, 100, 50), box(0, 0, 100, 50)), null, 'identical boxes');
  assert.equal(flipTransform(box(0, 0, 100, 50), box(0.2, -0.3, 100.02, 50.01)), null,
    'a sub-pixel difference is not an animation');
  assert.equal(flipTransform(null, box(0, 0, 100, 50)), null, 'no starting box (never measured)');
  assert.equal(flipTransform(box(0, 0, 0, 0), box(0, 0, 100, 50)), null, 'a collapsed starting box');
  assert.equal(flipTransform(box(0, 0, 100, 50), box(0, 0, 100, 0)), null, 'a collapsed target box');
});

test('the flight is long and hard-eased-out, and CSS lifts the element for it', () => {
  assert.ok(FLIP_MS >= 500, 'a full-window stretch needs room to read as deliberate');
  // An ease-out whose first control point is low and second is pinned at 1: most of
  // the distance early, coasting into the landing.
  const [x1, y1, , y2] = FLIP_EASING.match(/[\d.]+/g).map(Number);
  assert.ok(x1 < 0.3 && y1 > 0.9 && y2 === 1, `${FLIP_EASING} should ease hard out`);

  const css = COMPONENTS_CSS;
  const flight = css.slice(css.indexOf(`.canvas-viewport.${FLIP_ACTIVE_CLASS} {`));
  // Scrollbars belong to the settled state — appearing/disappearing mid-scale IS the flicker.
  assert.ok(/overflow: hidden !important/.test(flight.slice(0, 300)), 'no scrollbars mid-flight');
  // LEAVING starts scaled far beyond the in-flow box, so it must paint over the page
  // it is shrinking away from — otherwise it slides under the toolbars.
  const lifted = css.slice(css.indexOf(`body:not(.fullscreen-mode) .canvas-viewport.${FLIP_ACTIVE_CLASS} {`));
  assert.ok(/position: relative/.test(lifted.slice(0, 300)) && /z-index: 100\d\d/.test(lifted.slice(0, 300)),
    'the shrinking viewport is lifted above the restored page chrome');
});

// ── The CSS half of the contract ────────────────────────────────────────────
test('animations.css: reveal rest state, drop landing, and reduced-motion opt-outs', () => {
  const css = ANIMATIONS_CSS;
  const rest = css.slice(css.indexOf('.reveal-item.reveal-masked {'),
                         css.indexOf('\n}', css.indexOf('.reveal-item.reveal-masked {')));
  const base = css.slice(css.indexOf('.reveal-item {'), css.indexOf('\n}', css.indexOf('.reveal-item {')));
  // The mask is mounted only on rows straddling an edge: four gradient layers on every
  // row is what made a long transcript stutter while scrolling.
  assert.ok(!/mask-image/.test(base), 'a settled row composites no mask at all');
  // Scrolling is continuous, so the reveal is a one-element mask dissolve, never a clone
  // per particle.
  assert.ok(/--dissolve: 1/.test(base), 'out-of-view rows rest fully dissolved');
  // The mask keeps the STILL-VISIBLE span solid, so only the clipped edge is sanded.
  assert.ok(/var\(--vis-start/.test(rest) && /var\(--vis-end/.test(rest),
    'the wipe is anchored to the visible span, not to the row');
  assert.ok(/transform: none/.test(base), 'and a readable row is never displaced');
  // --dissolve must be REGISTERED or it cannot be transitioned — it would jump.
  assert.ok(/@property --dissolve \{ syntax: "<number>"/.test(css), '--dissolve is a registered property');
  assert.ok(/transition: --dissolve/.test(base), 'the dissolve is a transition, not a jump');
  // Union, not intersect: intersect would punch dot-holes through a settled row.
  assert.ok(/mask-composite: add/.test(rest), 'the grain and the wipe are UNIONed');
  assert.ok(/radial-gradient/.test(rest), 'a tiled dot grain');
  assert.ok(/linear-gradient\(to bottom, transparent 0, transparent var\(--vis-start/.test(rest),
    'plus a wipe spanning the still-visible run of the row');
  assert.ok(/\.reveal-item\.reveal-in \{ --dissolve: 0; transform: none; \}/.test(css), 'revealed rows are whole');
  // Transcript rows must NOT also carry an `animation: … both`: its filled end state
  // would out-rank the reveal's transform and pin every row at rest.
  assert.ok(!/\.chat-msg[^{]*\{[^}]*animation: chatMsgIn/.test(css), 'chat rows ride the reveal, not chatMsgIn');
  assert.ok(/\.chat-attach-chip \{ animation: chatMsgIn/.test(css), 'attachment chips keep their own entrance');
  // Drop landing: the overlay animates out (click-through while it does) and the canvas in.
  assert.ok(/#global-drop-overlay\.drop-closing \{[\s\S]*?pointer-events: none/.test(css),
    'the closing overlay cannot swallow the drop it is dismissing for');
  assert.ok(/animation: dropOverlayOut/.test(css), 'overlay leaves on an animation');
  assert.ok(/\.canvas-container\.drop-landing \{[\s\S]*?animation: canvasLand/.test(css), 'the canvas reveals on a drop');
  // Every piece of this collapses under prefers-reduced-motion.
  assert.ok(/@media \(prefers-reduced-motion: reduce\) \{[\s\S]{0,300}?\.reveal-item \{ --dissolve: 0 !important;[\s\S]{0,160}?mask-image: none !important; \}/.test(css),
    'reduced motion shows every row whole, mask and all');
  const reduced = css.slice(css.indexOf('@media (prefers-reduced-motion: reduce) {',
    css.indexOf('.canvas-container.drop-landing')));
  const block = reduced.slice(0, reduced.indexOf('}\n'));
  for (const sel of ['#global-drop-overlay.drop-closing', '.canvas-container.drop-landing',
                     '.canvas-container.drop-arriving'])
    assert.ok(block.includes(sel), `reduced motion drops ${sel}`);
});

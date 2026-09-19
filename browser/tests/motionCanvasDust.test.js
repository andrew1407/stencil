// playCanvasArrival (js/ui/motion.js): the gathered dust, its landing fallback and the
// animations.css blocks for the icon triggers and the canvas waiting behind its own motes.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import {
  flashLanding, LANDING_CLASS, ghostIn, hasPixels, playCanvasArrival, ASSEMBLING_CLASS,
  CLEARING_CLASS, GHOST_MS,
} from '../js/ui/motion.js';
import { ANIMATIONS_CSS } from './helpers/css.js';
import { box, el } from './helpers/motionRig.js';

// Every route — file load and project reopen — plays the arrival through playCanvasArrival.
test('playCanvasArrival gathers the dust when it can, and hides the canvas for exactly that long', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const vp = el();
  const box = el();
  assert.equal(playCanvasArrival({}, { viewport: vp, container: box, ghost: () => true }), 'dust');
  assert.ok(vp.has(ASSEMBLING_CLASS), 'the viewport holds the canvas back while the motes gather');
  assert.ok(!box.has(LANDING_CLASS), 'and the flight is NOT played on top of it');
  t.mock.timers.tick(GHOST_MS - 1);
  assert.ok(vp.has(ASSEMBLING_CLASS), 'still up while the dust flies');
  t.mock.timers.tick(2);
  assert.ok(!vp.has(ASSEMBLING_CLASS), 'and the canvas is handed back at the end');
});

test('playCanvasArrival falls back to the landing flight when the dust cannot play', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const vp = el();
  const box = el();
  assert.equal(playCanvasArrival({}, { viewport: vp, container: box, ghost: () => false }), 'flight');
  assert.ok(box.has(LANDING_CLASS), 'the container plays the plain landing instead');
  assert.ok(!vp.has(ASSEMBLING_CLASS), 'and the canvas is never hidden — there is no dust to hide behind');
  t.mock.timers.tick(701);
  assert.ok(!box.has(LANDING_CLASS), 'one shot, then the class comes off');
});

test('playCanvasArrival: reduced motion still ends in the right state', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const prior = globalThis.matchMedia;
  globalThis.matchMedia = () => ({ matches: true });
  try {
    const vp = el();
    const box = el();
    // The real ghostIn — it is what declines under the preference, and the canvas must
    // NOT be hidden when nothing is going to gather in front of it.
    assert.equal(playCanvasArrival({ width: 100, height: 80, parentElement: {} },
      { viewport: vp, container: box }), 'flight');
    assert.ok(!vp.has(ASSEMBLING_CLASS), 'never hidden behind dust that will not play');
    t.mock.timers.tick(701);
    assert.ok(!box.has(LANDING_CLASS), 'and no class is left behind');
  } finally { globalThis.matchMedia = prior; }
});

test('playCanvasArrival survives having no DOM to reach for', () => {
  assert.doesNotThrow(() => playCanvasArrival(null));
});

test('both routes that put an image on the canvas play the arrival', () => {
  const loader = readFileSync(new URL('../js/core/imageSettle.js', import.meta.url), 'utf8');
  assert.match(loader, /playCanvasArrival\(app\.canvas, \{ from: opts\.from \}\)/,
    'a freshly loaded file arrives');
  const storage = readFileSync(new URL('../js/core/storage.js', import.meta.url), 'utf8');
  // The open passes it; the cross-tab sync path (the other caller) deliberately does not.
  assert.match(storage, /this\.loadPayloadIntoApp\(proj\.payload, \{ landing: true \}\)/,
    'opening a saved project arrives too — this is the regression');
  assert.match(storage, /if \(landing\) playCanvasArrival\(this\.app\.canvas\);/,
    'and it is played once the restored image is in the backing store');
  assert.match(storage, /syncActiveFromStorage\(\)[\s\S]*?this\.loadPayloadIntoApp\(payload\);/,
    'a peer\'s edit syncing in from another tab stays still — no landing option');
  // The clear-dissolve twin runs off the same pair of class names; keep them in step.
  assert.equal(CLEARING_CLASS, 'canvas-clearing');
  assert.equal(ASSEMBLING_CLASS, 'canvas-assembling');
  assert.match(storage, /flashLanding\(vp, 'canvas-clearing', GHOST_MS\)/,
    'and the clear still dissolves the outgoing image');
});

test('hasPixels tells a painted canvas from an empty one', () => {
  const ctx = (fill) => ({ getImageData: (_x, _y, w, h) => ({ data: fill(w * h * 4) }) });
  const empty = ctx((n) => new Uint8ClampedArray(n));
  assert.equal(hasPixels(empty, 400, 300), false, 'a blank canvas has nothing to make motes of');
  const painted = ctx((n) => { const d = new Uint8ClampedArray(n); d.fill(255); return d; });
  assert.equal(hasPixels(painted, 400, 300), true);
  // One opaque pixel is still an image — but the stride must actually be able to see it,
  // so the sample walks the buffer rather than peeking at the corner.
  const sparse = ctx((n) => { const d = new Uint8ClampedArray(n); d[4 * 41 * 7 + 3] = 255; return d; });
  assert.equal(hasPixels(sparse, 400, 300), true);
  assert.equal(hasPixels(null, 10, 10), false, 'no context');
  assert.equal(hasPixels(empty, 0, 0), false, 'no box');
});

// Each icon mimes its own action (config/iconMotion.json is canonical, iconMotion.test.js
// pins it); what is shared is the trigger: the `--ic-on` latch and `--ic-play`.
test('animations.css: one trigger drives every icon, and the generic tilt is gone', () => {
  const css = ANIMATIONS_CSS;
  assert.ok(!/rotate\(-7deg\) scale\(1\.14\)/.test(css),
    'the one-size-fits-all tilt+swell must not come back');
  // `.ctx-item` can carry a nested `.ctx-sub` flyout, so its branch is scoped
  // (`> .ctx-icon`/`> .ctx-check`); a descendant match would reach every icon inside it.
  const triggerStart = css.indexOf(':is(button, .btn-icon');
  assert.ok(triggerStart >= 0, 'the icon-hover trigger exists');
  const triggerBraceAt = css.indexOf('{', triggerStart);
  const selector = css.slice(triggerStart, triggerBraceAt);
  const body = css.slice(triggerBraceAt + 1, css.indexOf('}', triggerBraceAt));
  assert.match(selector, /:hover\s*\n\s*:is\(\.ic, \.draw-mode-icon, \.ic \*, \.draw-mode-icon \*\)/,
    'the general branch reaches the PARTS, not just the glyph');
  assert.match(selector, /\.ctx-item:not\(\.is-loading\):not\(\.swapping\):not\(\[id\^="toggle-"\]\):hover\s*\n\s*> :is\(\.ctx-icon, \.ctx-check\)/,
    "a submenu parent's hover reaches only its OWN icon, never a nested flyout's");
  assert.match(body, /--ic-on: 1;/, 'it flips the hold latch');
  assert.match(body, /animation-name: var\(--ic-play, none\);/, 'and starts the settle play');
  assert.ok(!/transform:/.test(body),
    'the trigger itself moves nothing — the per-icon rules do');
  // The fold chevrons opt out as an ATTRIBUTE, so the rule stays at class specificity
  // and the reduced-motion override below can still outrank it.
  assert.match(css, /:not\(\[id\^="toggle-"\]\):hover/, 'the fold chevrons keep their own idiom');
  assert.ok(!/:not\(#toggle-coord-panel\)/.test(css),
    'as an attribute, so the rule stays at class specificity');
  // A face mid-swap owns its own animation-name; the trigger must not steal it.
  assert.match(css, /:not\(\.is-loading\):not\(\.swapping\)/);
  // A glyph already animating owns its transform outright.
  assert.match(css, /\.is-loading \.ic, \.is-loading \.ic \*, \.swapping > svg, \.swapping > svg \* \{ transition: none; \}/);
});

test('animations.css: reduced motion cancels the icon hover but not the chevrons’ state', () => {
  const css = ANIMATIONS_CSS;
  const block = css.match(/@media \(prefers-reduced-motion: reduce\) \{[\s\S]*?--ic-on: 0 !important;[\s\S]*?\n\}/);
  assert.ok(block, 'the hover move is cancelled under the preference');
  // Killing the latch and the keyframe switch leaves every glyph in its rest pose —
  // which is also where every motion here ends, so the end state stays correct.
  assert.match(block[0], /animation-name: none !important;/);
  assert.match(block[0], /transition: none !important;/);
  // The rotate(180deg) that says "this panel is folded" is STATE. An `!important`
  // transform reset here would flatten it and the chevron would point the wrong way.
  assert.ok(!/transform: none !important/.test(block[0]),
    'nothing here outranks the fold chevrons’ own rotation');
});

test('animations.css: the canvas waits behind its own dust, both directions', () => {
  const css = ANIMATIONS_CSS;
  assert.match(css, /\.canvas-viewport\.canvas-clearing #canvas \{ opacity: 0; \}/,
    'hidden while the dust falls');
  assert.match(css, /\.canvas-viewport\.canvas-assembling #canvas \{ opacity: 0; transition: none; \}/,
    'and while the dust gathers — or the finished picture sits behind its own motes');
  // The hide is instant — the shared transition below is for the fade back UP only.
  assert.match(css, /\.canvas-viewport \.idle-create, \.canvas-viewport #canvas \{ transition: opacity/,
    'the fade back up is still a transition');
});

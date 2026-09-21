// The wipe the surfaces share (js/ui/motion.js + animations.css): one duration and one
// ease-out curve across browser and extension, the panel folds, and no remembered pointer.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { THEME_SWAP_MS, originOfId } from '../js/ui/motion.js';
import { motionSource } from './helpers/motionSource.js';
import { ANIMATIONS_CSS, extensionAnimationsCss } from './helpers/css.js';

// One wipe shared by the browser, the extension and the desktop's Qt overlay: same length
// and a true ease-out curve, never easing in.
test('the wipe: browser and extension share one duration and one ease-out curve', () => {
  const decls = [['browser', ANIMATIONS_CSS],
                 ['extension', extensionAnimationsCss()]]
    .map(([name, text]) => {
      const decl = /animation: themeSwapReveal var\(--swap-ms, (\d+)ms\) cubic-bezier\(([^)]*)\)/
        .exec(text);
      assert.ok(decl, `${name}: the reveal declaration`);
      return { name, ms: Number(decl[1]), curve: decl[2].split(',').map(Number) };
    });
  const [browser, extension] = decls;
  assert.deepEqual(extension, { ...extension, ms: browser.ms, curve: browser.curve },
    'the two stylesheets drifted apart');
  // The JS timer that clears the classes has to outlast the CSS, or the fallback
  // cross-fade is cut off mid-way.
  assert.equal(THEME_SWAP_MS, browser.ms, 'motion.js THEME_SWAP_MS is the CSS fallback');
  const ext = readFileSync(new URL('../../browser-extension/src/lib/dust/swapGeometry.js', import.meta.url), 'utf8');
  assert.equal(Number(/const SWAP_MS = (\d+)/.exec(ext)[1]), browser.ms, 'swapGeometry.js SWAP_MS agrees');

  // Judged on the AREA it sweeps, not its control points: a circle's area grows as r², so the
  // radius has to ease IN slightly.
  const [x1, y1, x2, y2] = browser.curve;
  const at = (t) => {                    // solve x(u) = t, then take y(u)
    let lo = 0, hi = 1, u = t;
    for (let i = 0; i < 40; i++) {
      u = (lo + hi) / 2;
      const x = 3 * (1 - u) ** 2 * u * x1 + 3 * (1 - u) * u * u * x2 + u ** 3;
      if (x < t) lo = u; else hi = u;
    }
    return 3 * (1 - u) ** 2 * u * y1 + 3 * (1 - u) * u * u * y2 + u ** 3;
  };
  assert.equal(y2, 1, `${browser.curve}: the sweep ends at the full radius`);
  // Fraction of a viewport covered by a circle of radius `r*R` about (cx, cy), sampled.
  const coveredAt = (t, cx, cy, w = 1440, h = 900) => {
    const R = Math.hypot(Math.max(cx, w - cx), Math.max(cy, h - cy));
    const r = at(t) * R;
    let inside = 0, n = 90;
    for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) {
      const x = (i + 0.5) * w / n, y = (j + 0.5) * h / n;
      if ((x - cx) ** 2 + (y - cy) ** 2 <= r * r) inside++;
    }
    return inside / (n * n);
  };
  // Every origin the app actually uses: a toolbar icon near a corner, and the centre
  // fallback for when no control is on screen.
  for (const [name, cx, cy] of [['toolbar icon', 93, 436], ['viewport centre', 720, 450]]) {
    // THE regression: nothing must be finished early, or the rest of the duration is dead
    // time with a stalled-looking crescent left in the corner.
    assert.ok(coveredAt(0.8, cx, cy) < 0.92, `${name}: still visibly moving at 80% of the time`);
    assert.ok(coveredAt(0.9, cx, cy) < 0.99, `${name}: and at 90%`);
    // …and the opposite trap: it must not creep at the start and then rush.
    assert.ok(coveredAt(0.4, cx, cy) > 0.15, `${name}: away from the button without creeping`);
    // Even growth: no decile may sweep more than a third of the screen on its own.
    let prev = 0;
    for (let t = 0.1; t <= 1.0001; t += 0.1) {
      const c = coveredAt(Math.min(t, 1), cx, cy);
      assert.ok(c >= prev, `${name}: the wipe never goes backwards`);
      assert.ok(c - prev < 0.34, `${name}: no decile floods a third of the screen at once`);
      prev = c;
    }
  }
});

// The fold and the panel slide keep a plain ease-out: they grow along one axis, so their
// area is linear in time, unlike the circular wipe's.
test('the panel folds are an ease-out, and hold visibility for the whole fold', () => {
  const css = ANIMATIONS_CSS;
  const token = (name) => new RegExp(`--${name}:\\s*([^;]+);`).exec(css)?.[1].trim();
  const [, y1, , y2] = /cubic-bezier\(([^)]*)\)/.exec(token('fold-ease'))[1].split(',').map(Number);
  assert.ok(y1 > 0.5 && y2 === 1, `--fold-ease ${token('fold-ease')}: leaves at speed, settles at the end`);
  const foldMs = Number(/(\d+)ms/.exec(token('fold-ms'))[1]);
  assert.ok(foldMs >= 300, 'the fold is the slower, settling kind');
  // …and COLLAPSING is slower still: with no icon to shrink into, the fold itself is the
  // only thing that reads as the menu leaving, so a brisk exit registers as a snap.
  const foldOutMs = Number(/(\d+)ms/.exec(token('fold-out-ms'))[1]);
  assert.ok(foldOutMs > foldMs, `--fold-out-ms ${foldOutMs} outlasts the way back in`);
  // visibility is what takes the collapsed toolbar out of the tab order, so its delay is the
  // collapse's own duration — released early, the fold animates against nothing.
  const hidden = css.slice(css.indexOf('#controls-body.hidden {'));
  assert.match(hidden.slice(0, hidden.indexOf('}')), /visibility 0s linear var\(--fold-out-ms\)/,
    'the visibility delay must be the fold duration itself, not a copied constant');
  // Every collapsing part opts out under reduced motion.
  const reduced = css.slice(css.indexOf('@media (prefers-reduced-motion: reduce) {\n    #controls-body'));
  for (const sel of ['#controls-body', '.coordinates-panel', '#coord-body', '.coord-tabs'])
    assert.ok(reduced.slice(0, reduced.indexOf('}')).includes(sel), `${sel} opts out`);
});

test('motion.js keeps no pointer state for the swap to fall back to', () => {
  const src = motionSource();
  assert.ok(!/addEventListener\(\s*'pointerdown'/.test(src),
    'a remembered press is what put the wipe in the corner — the control is the only origin');
});

test('originOfId is null when every copy is hidden, and never throws on a stub', () => {
  const prior = globalThis.document;
  globalThis.document = { querySelectorAll: () => [{ getBoundingClientRect: () => ({ width: 0, height: 0, left: 0, top: 0 }) }] };
  try { assert.equal(originOfId('theme-toggle'), null); } finally { globalThis.document = prior; }
  globalThis.document = {};
  try { assert.equal(originOfId('theme-toggle'), null); } finally { globalThis.document = prior; }
});

test('animations.css: only the wipe drives the transition — no UA group/old default', () => {
  const css = ANIMATIONS_CSS;
  assert.match(css, /::view-transition-group\(root\) \{ animation: none; \}/,
    'the UA group animation would retime the pair under the wipe');
  assert.match(css, /::view-transition-old\(root\) \{ z-index: 0; animation: none; \}/);
});

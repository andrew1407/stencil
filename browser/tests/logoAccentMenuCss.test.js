// The CSS contract pins (animations.css + components.css): the logo loop keys on the latch, the
// menu grows out of the logo, and an announced accent change re-marks an open list.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { motionSource } from './helpers/motionSource.js';
import { COMPONENTS_CSS, ANIMATIONS_CSS } from './helpers/css.js';
import { rig, marked } from './helpers/logoAccentMenuRig.js';

// ── CSS contract pins (same style as motion.test.js) ────────────────────
test('animations.css keys the logo loop on the latch, not :hover', () => {
  const css = ANIMATIONS_CSS;
  assert.match(css, /\.app-logo-wrap\.logo-hover \.app-logo \{ animation: logoPulse/,
    'pulse keyed on .logo-hover (a :hover-gated animation is cancelled by the accent view transition)');
  assert.match(css, /\.app-logo-wrap\.logo-hover::before/,
    'rays ride the same latch');
  assert.ok(!/\.app-logo:hover \{ animation/.test(css),
    'no :hover-gated animation on the logo remains');
});

test('the menu grows OUT OF the logo and shrinks back INTO it — anchored, not a generic rise', () => {
  const css = ANIMATIONS_CSS;
  // A shared, named pair: any menu hanging off a control can use it by pointing
  // transform-origin at the edge it is anchored to.
  const from = /@keyframes menuFromAnchor \{[^}]*\}[^}]*\}[^}]*\}/.exec(css)?.[0] || '';
  const to = /@keyframes menuToAnchor \{[^}]*\}[^}]*\}[^}]*\}/.exec(css)?.[0] || '';
  assert.match(from, /scale\(0\.66\)/, 'it starts small enough to read as coming from the mark');
  assert.match(to, /scale\(0\.66\)/, 'and the exit is the reverse — back into it');
  assert.ok(/0%\s*\{ opacity: 0;/.test(from) && /22%\s*\{ opacity: 1; \}/.test(from),
    'full opacity while still small (modalFromIcon\'s trick) — you watch it grow, not fade');
  assert.ok(/60%\s*\{ opacity: 0\.85; \}/.test(to),
    'the shrink stays visible before the fade takes over');
  // Origin = the corner nearest the control. The logo copy hangs LEFT-aligned under the
  // logo; the Visuals copy hangs right-aligned under its trigger.
  assert.match(css, /\.accent-dd-menu \{ transform-origin: right top; \}/,
    'the shared copy scales out of its trigger\'s corner');
  assert.match(css, /\.logo-accent-menu, \.cs-dd \.accent-dd-menu \{ transform-origin: left top; \}/,
    'the left-aligned copies scale out of theirs (a centred origin is what made it read as no motion)');
  assert.match(css, /\.accent-dd-menu:not\(\[hidden\]\) \{ animation: menuFromAnchor 0\.22s cubic-bezier\(0\.16, 1, 0\.3, 1\)/,
    'entrance: the app\'s existing menu easing, in its usual duration range');
  assert.match(css, /\.logo-accent-menu\.dd-closing \{ animation: menuToAnchor 0\.18s ease/,
    'exit: the reverse, on the same easing the other pop-outs use');
  // Reduced motion drops BOTH — toolbar.js then hides it outright (test above), so a
  // neutralised exit can't leave the menu sitting on screen for the fallback timer.
  const reduced = /@media \(prefers-reduced-motion: reduce\) \{[^}]*\.accent-dd-menu[^}]*\}/.exec(css)?.[0] || '';
  assert.match(reduced, /\.accent-dd-menu:not\(\[hidden\]\), \.logo-accent-menu\.dd-closing \{ animation: none; \}/,
    'entrance and exit are both neutralised under prefers-reduced-motion');
});

test('components.css lifts the shared 280px cap for the logo menu — toolbar.js re-caps per open', () => {
  const css = COMPONENTS_CSS;
  const block = /\.logo-accent-menu \{[^}]*\}/.exec(css)?.[0] || '';
  assert.match(block, /max-height: none/,
    'without this the full preset list scrolls even in a tall window (the shared cap is sized for the Visuals dialog)');
});

test('motion.js raises theme-instant BEFORE startViewTransition — the latch depends on it', () => {
  const src = motionSource();
  const add = src.indexOf('root.classList.add(THEME_INSTANT_CLASS)');
  const svt = src.indexOf('document.startViewTransition(');
  assert.ok(add !== -1 && svt !== -1 && add < svt,
    'the class must already be up when the swap\'s synthetic pointerleave lands');
});

// A logo click can cycle the preset with the list still open, so the controller announces the NEW value on
// window and an open menu re-marks on it; a closed one stops listening (user report).
test('an accent change announced while the menu shows moves the ✓; a closed menu ignores it', () => {
  const { wrap, menu, win, doc } = rig();
  wrap.dispatch('contextmenu', { preventDefault: () => {} });
  assert.deepEqual(marked(menu), ['violet']);
  win.dispatch('stencil:accent-changed', { detail: 'aqua' });
  assert.deepEqual(marked(menu), ['aqua'], 'the mark follows the announced preset, not the stale app.accent');
  win.dispatch('stencil:accent-changed', { detail: '#123456' });
  assert.deepEqual(marked(menu), [], 'a custom colour marks no preset row');
  win.dispatch('stencil:accent-changed', { detail: 'brown' });
  assert.deepEqual(marked(menu), ['brown']);
  // Closed: the listener is gone, so a later change touches nothing.
  doc.dispatch('keydown', { key: 'Escape' });
  win.dispatch('stencil:accent-changed', { detail: 'pink' });
  assert.deepEqual(marked(menu), ['brown'], 'no re-mark after close');
  // …and the controller really announces: setAccent / setCustomAccent both dispatch it.
  const src = readFileSync(new URL('../js/ui/accent/accentController.js', import.meta.url), 'utf8');
  assert.match(src, /setAccent\(key, originEl = null\) \{[\s\S]*?this\.announce\(next\);/);
  assert.match(src, /this\.announce\(norm\);/);
  assert.match(src, /publish\(EVENTS\.accentChanged, value\)/);
});

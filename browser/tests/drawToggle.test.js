// The Draw group's styling contract (Start/Stop is an accent toggle, not the global green
// `button.active`), the measured width pin, and the swap's CSS. From drawToggle.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { installDom, createStubElement, createStubDocument } from './helpers/dom.js';
import { swapContent, pinWidestFace } from '../js/ui/motion.js';
import { LAYOUT_CSS, ANIMATIONS_CSS } from './helpers/css.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const layoutCss = LAYOUT_CSS;
const animCss = ANIMATIONS_CSS;

// Every rule block in a sheet whose SELECTOR mentions `needle`.
const blocksFor = (css, needle) => [...css.matchAll(/([^{}]+)\{([^}]*)\}/g)]
  .filter((m) => m[1].includes(needle))
  .map((m) => ({ sel: m[1].trim(), body: m[2] }));

// ── 1. Styling: accent, never the green active light ────────────────────────

test('the draw toggle is accent, not the green button.active', () => {
  const blocks = blocksFor(layoutCss, '#draw-toggle');
  assert.ok(blocks.length >= 4, 'the draw toggle styles itself');
  for (const b of blocks) {
    assert.ok(!/--success/.test(b.body), `green must not reach the draw toggle (${b.sel})`);
    assert.ok(!/#[0-9a-fA-F]{3,6}\b(?!f)/.test(b.body.replace(/#fff\b/g, '')),
      `no hard-coded colour in ${b.sel} — the accent is the user's to change`);
  }
  assert.match(layoutCss, /#draw-toggle:not\(:disabled\) \{[^}]*background: transparent;[\s\S]*?border-color: var\(--ui-outline\);[\s\S]*?color: var\(--text-main\)/,
    'idle: the plain UI outline and the theme\'s own ink — the accent is the RUNNING state');
  assert.match(layoutCss, /#draw-toggle\.active:not\(:disabled\) \{[^}]*background: var\(--accent\)/,
    'drawing: accent-FILLED');
});

test('the filled state uses the app’s on-accent foreground, so a light accent stays legible', () => {
  assert.match(layoutCss, /#draw-toggle\.active:not\(:disabled\) \{[^}]*color: var\(--on-accent\)/,
    'the ink the accent picked for itself — white on a deep accent, near-black on a pale one');
  // Idle is accent-on-neutral: it reads as text, in the theme's own ink.
  assert.match(layoutCss, /#draw-toggle:not\(:disabled\) \{[^}]*color: var\(--text-main\)/);
});

test('the OTHER active controls keep the green light — the repaint was scoped', () => {
  assert.match(layoutCss, /button\.active \{\s*background: var\(--success\);\s*color: #fff;\s*\}/,
    'the global active rule is untouched — a fixed green ground, so white whatever the accent');
  // The controls that rely on it, by their own class toggles.
  assert.match(read('../js/ui/fullscreen/layer.js'), /fsBtn\.classList\.toggle\('active', isFullscreen\)/);
  assert.match(read('../js/core/drawingApp.js'), /btn\.classList\.toggle\('active', this\.storage\.incognito\)/);
  for (const id of ['#fullscreen-toggle', '#incognito-toggle']) {
    assert.equal(blocksFor(layoutCss, `${id}.active`).length, 0, `${id} was not repainted`);
  }
});

test('disabled still greys out, and the outline costs no width', () => {
  assert.match(layoutCss, /button\.active:disabled,\s*button\.primary:disabled \{[\s\S]*?background: var\(--disabled-bg\)/,
    'every filled variant is named, or a fill out-specifies :disabled and reads as clickable');
  // Every accent declaration is gated on :not(:disabled), so a disabled draw toggle falls
  // through to button:disabled instead of out-specifying it with its id.
  for (const b of blocksFor(layoutCss, '#draw-toggle')) {
    if (/var\(--accent\)|background: transparent/.test(b.body)) {
      assert.ok(b.sel.includes(':not(:disabled)'), `${b.sel} must not paint a disabled button`);
    }
  }
  assert.match(layoutCss, /#draw-toggle \{\s*border: 1px solid transparent;\s*\}/,
    'the border is always there (transparent when off), so no state changes the box');
  assert.match(layoutCss, /\* \{[^}]*box-sizing: border-box/, 'border-box: the 1px outline is inside the pinned width');
});

// The width pin is measured per button at the real font, not one hard-coded rem for both pairs; what
// must still hold is that a toggle never changes its own width.

test('the stylesheet no longer guesses a width for the pair', () => {
  const block = blocksFor(layoutCss, '.btn-draw-fixed')
    .find((b) => b.sel.split('\n').pop().trim() === '.btn-draw-fixed');
  assert.ok(block, '.btn-draw-fixed still owns the box');
  assert.ok(!/[^-]width:\s*[\d.]+(rem|px|em|ch)/.test(block.body),
    'no hard-coded width — a magic number only ever fits one font and one language');
  assert.match(block.body, /min-width: max-content;/, 'the floor is the content itself');
  assert.match(block.body, /justify-content: center;/, 'and the face stays centred in the box');
});

// A button that measures a face the way a browser would: a fixed box plus the label's
// length. The two faces of a pair differ, which is the whole problem.
const measurableBtn = () => {
  const el = createStubElement('button');
  el.getBoundingClientRect = () => ({
    width: el.style.width === 'auto'
      ? 26.4 + 7 * (el.innerHTML.match(/<span>([^<]*)</)?.[1].length || 0)
      : parseFloat(el.style.width) || 0,
  });
  return el;
};

test('pinWidestFace pins the WIDER face, in px, and only measures once', () => {
  const doc = createStubDocument();
  const btn = measurableBtn();
  const faces = ['<svg></svg><span>Start</span>', '<svg></svg><span>Stop</span>'];
  btn.innerHTML = faces[0];
  const px = pinWidestFace(btn, faces, { doc });
  assert.equal(px, 62, '26.4 + 5 chars, rounded up — "Start", not "Stop"');
  assert.equal(btn.style.width, '62px');
  assert.equal(btn.innerHTML, faces[0], 'the face on show is put back after the probe');
  // Once per element: syncDrawToggleUI runs on every isDrawing change and must not
  // re-measure the world each time. A re-rendered toolbar hands over a NEW node.
  assert.equal(pinWidestFace(btn, faces, { doc }), 0, 'the second call is a no-op');
  assert.equal(pinWidestFace(btn, faces, { doc, force: true }), 62, 'unless forced (webfonts)');
  assert.equal(pinWidestFace(measurableBtn(), faces, { doc }), 62, 'a fresh node re-measures');
});

test('pinWidestFace leaves the CSS floor alone when it cannot measure', () => {
  const doc = createStubDocument();
  // No layout (a stub element measures 0) — pinning 0px would collapse the button.
  const flat = createStubElement('button');
  assert.equal(pinWidestFace(flat, ['<span>Start</span>'], { doc }), 0);
  assert.equal(flat.style.width, undefined, 'no inline width at all');
  assert.equal(pinWidestFace(null, ['x'], { doc }), 0);
  assert.equal(pinWidestFace(measurableBtn(), [], { doc }), 0);
});

test('a face swap never changes the button’s width', () => {
  const doc = createStubDocument();
  const btn = measurableBtn();
  const faces = ['<svg></svg><span>Start</span>', '<svg></svg><span>Stop</span>'];
  pinWidestFace(btn, faces, { doc });
  const pinned = btn.style.width;
  const win = installDom({}, {});
  try {
    swapContent(btn, faces[0], { key: 'start' });          // first paint
    swapContent(btn, faces[1], { key: 'stop', setTimer: () => 0 });
    assert.equal(btn.style.width, pinned, 'the box is the same after Start→Stop');
    swapContent(btn, faces[0], { key: 'start', setTimer: () => 0 });
    assert.equal(btn.style.width, pinned, 'and after Stop→Start');
  } finally { win.restore?.(); }
});

test('both Draw-group toggles pin themselves from their own two faces', () => {
  const src = read('../js/ui/panel/drawToggleUI.js');
  for (const [sync, faces] of [
    ['syncDrawToggleUI', /pinWidestFace\(btn, \[face\(false\), face\(true\)\]\);/],
    ['syncDrawModeUI', /pinWidestFace\(btn, \[face\(false\), face\(true\)\]\);/],
  ]) {
    const body = src.slice(src.indexOf(`${sync} = (app)`), src.indexOf(`${sync} = (app)`) + 1200);
    assert.match(body, faces, `${sync} pins from BOTH faces, not the one on show`);
    assert.ok(body.indexOf('pinWidestFace') < body.indexOf('swapContent'),
      `${sync} pins the box before the face moves into it`);
  }
});

// ── 2. The swap: CSS ────────────────────────────────────────────────────────

test('animations.css: the ghost is absolute, so nothing moves mid-swap', () => {
  assert.match(animCss, /\.swap-ghost \{[^}]*position: absolute;[^}]*inset: 0;/,
    'the outgoing face is stacked ON the new one, out of layout flow');
  assert.match(animCss, /\.swap-ghost \{[^}]*pointer-events: none/, 'and it never swallows a click');
  assert.match(animCss, /\.swapping \{ position: relative; \}/, 'the host is the ghost’s containing block');
  assert.match(animCss, /@keyframes swapGlyphIn \{ from \{ opacity: 0; transform: rotate\(-115deg\)/,
    'the glyph turns in');
  assert.match(animCss, /@keyframes swapLabelIn \{ from \{ opacity: 0; transform: translateY\(/, 'the word rises');
  assert.match(animCss, /\.swapping > span:not\(\.swap-ghost\)/, 'the ghost is excluded from the entering animation');
  assert.ok(!/\.swapping[^}]*width:/.test(animCss), 'the swap never touches the pinned width');
});

test('animations.css: reduced motion leaves the new face and no motion', () => {
  const reduced = animCss.slice(animCss.indexOf('.swapping > svg, .swapping > span'));
  assert.match(reduced.slice(0, 200), /animation: none/);
  assert.match(animCss, /@media \(prefers-reduced-motion: reduce\) \{\s*\.swapping > svg[^}]*\}\s*\.swap-ghost \{ display: none; animation: none; \}/);
});

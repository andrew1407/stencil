// The Draw group's two toggles — Start/Stop (#draw-toggle) and Line/Rect (#draw-mode-toggle).
//
// Two things are pinned here:
//   1. The STYLING contract. Start/Stop is an accent toggle, not a status light: the global
//      green `button.active` (layout.css) is a "this control is on" light for the other
//      toggles (#fullscreen-toggle, #incognito-toggle, the chat button) and the draw button
//      opted out of it — accent-outlined idle, accent-filled while drawing, with the app's
//      on-accent foreground so a light accent stays legible. Green must not come back, and
//      the other .active controls must not have been repainted along with it.
//   2. The shared face SWAP (js/ui/motion.js swapContent + animations.css). Both toggles
//      rewrite themselves in place; the transition is decoration layered over a synchronous
//      write, so rapid toggling (holding Alt+A/Alt+S) can never leave a stale glyph, a
//      doubled label, or a stuck animation — the face always matches isDrawing / drawMode.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { installDom, createStubElement, createStubDocument } from './helpers/dom.js';
import { swapContent, pinWidestFace, SWAP_CLASS, SWAP_GHOST_CLASS, SWAP_MS } from '../js/ui/motion.js';

// The two sync methods are instance methods that only touch the DOM + hotkeys, so they are
// driven via `.call(mock)` on a stub document (the drawingApp-launch.test.js convention).
const doc = installDom({}, { location: { hash: '', pathname: '/app', search: '' }, history: { replaceState: () => {} } });
const { DrawingApp } = await import('../js/core/drawingApp.js');

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const layoutCss = read('../css/layout.css');
const animCss = read('../css/animations.css');

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
  assert.match(read('../js/ui/fullscreenLayer.js'), /fsBtn\.classList\.toggle\('active', isFullscreen\)/);
  assert.match(read('../js/core/drawingApp.js'), /btn\.classList\.toggle\('active', this\.storage\.incognito\)/);
  for (const id of ['#fullscreen-toggle', '#incognito-toggle']) {
    assert.equal(blocksFor(layoutCss, `${id}.active`).length, 0, `${id} was not repainted`);
  }
});

test('disabled still greys out, and the outline costs no width', () => {
  assert.match(layoutCss, /button\.active:disabled \{[\s\S]*?background: var\(--disabled-bg\)/,
    'the disabled rule still catches an active button');
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

// ── 1b. The pinned box: measured, not guessed ───────────────────────────────
// The pin used to be one hard-coded `width: 5.5rem` for BOTH pairs — a guess, so short
// labels ("Start", "Line") floated in dead space. It is now measured per button at the
// real font. What must still hold: a toggle never changes its own width.

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
  const src = read('../js/core/drawingApp.js');
  for (const [sync, faces] of [
    ['syncDrawToggleUI', /pinWidestFace\(btn, \[face\(false\), face\(true\)\]\);/],
    ['syncDrawModeUI', /pinWidestFace\(btn, \[face\(false\), face\(true\)\]\);/],
  ]) {
    const body = src.slice(src.indexOf(`${sync}()`), src.indexOf(`${sync}()`) + 1200);
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

// ── 3. The swap: helper logic ───────────────────────────────────────────────

// A button whose querySelectorAll actually resolves its ghost children (the stub's
// default returns []), so the "drop a ghost still in flight" path is exercised.
const makeBtn = () => {
  const el = createStubElement('button');
  el.querySelectorAll = () => el.children.filter((c) => c.classes?.has?.(SWAP_GHOST_CLASS));
  return el;
};
const ghostsOf = (el) => el.children.filter((c) => c.classes?.has?.(SWAP_GHOST_CLASS));
// Collects the helper's timers so a test can fire them when it chooses.
const timerBox = () => {
  const q = [];
  return { setTimer: (fn) => q.push(fn), flush: () => { const all = q.splice(0); all.forEach((fn) => fn()); } };
};

test('the first paint just writes the face — nothing to swap from', () => {
  const btn = makeBtn();
  const t = timerBox();
  assert.equal(swapContent(btn, '<span>Start</span>', { key: 'start', setTimer: t.setTimer }), false);
  assert.equal(btn.innerHTML, '<span>Start</span>');
  assert.equal(btn.classes.has(SWAP_CLASS), false);
  assert.equal(ghostsOf(btn).length, 0);
});

test('a changed face writes FIRST, then plays — the old face leaves as a ghost', () => {
  const btn = makeBtn();
  const t = timerBox();
  swapContent(btn, '<span>Start</span>', { key: 'start', setTimer: t.setTimer });
  assert.equal(swapContent(btn, '<span>Stop</span>', { key: 'stop', setTimer: t.setTimer }), true);
  assert.equal(btn.innerHTML, '<span>Stop</span>', 'the DOM is never behind the state');
  assert.equal(btn.classes.has(SWAP_CLASS), true);
  const [ghost] = ghostsOf(btn);
  assert.equal(ghost.innerHTML, '<span>Start</span>', 'the ghost carries the face that left');
  assert.equal(ghost.getAttribute('aria-hidden'), 'true', 'a duplicate label must not reach a screen reader');
  t.flush();
  assert.equal(btn.classes.has(SWAP_CLASS), false, 'the animation class is cleaned up');
  assert.equal(ghostsOf(btn).length, 0, 'and so is the ghost');
});

test('an unchanged face is a no-op — updateButtons runs on every redraw', () => {
  const btn = makeBtn();
  const t = timerBox();
  swapContent(btn, '<span>Start</span>', { key: 'start', setTimer: t.setTimer });
  btn.innerHTML = 'TOUCHED';   // a rewrite would clobber this; a no-op leaves it
  for (let i = 0; i < 5; i++) {
    assert.equal(swapContent(btn, '<span>Start</span>', { key: 'start', setTimer: t.setTimer }), false);
  }
  assert.equal(btn.innerHTML, 'TOUCHED');
  assert.equal(ghostsOf(btn).length, 0, 'no ghost, no animation, no churn');
});

test('a fresh element (a re-rendered toolbar) paints rather than being skipped as unchanged', () => {
  const t = timerBox();
  const first = makeBtn();
  swapContent(first, '<span>Start</span>', { key: 'start', setTimer: t.setTimer });
  const rebuilt = makeBtn();
  swapContent(rebuilt, '<span>Start</span>', { key: 'start', setTimer: t.setTimer });
  assert.equal(rebuilt.innerHTML, '<span>Start</span>');
});

test('reduced motion: the new face, instantly, with no ghost and no class', () => {
  const btn = makeBtn();
  const t = timerBox();
  const opts = { setTimer: t.setTimer, reduced: () => true };
  swapContent(btn, '<span>Start</span>', { key: 'start', ...opts });
  assert.equal(swapContent(btn, '<span>Stop</span>', { key: 'stop', ...opts }), false);
  assert.equal(btn.innerHTML, '<span>Stop</span>', 'the final state is still correct');
  assert.equal(btn.classes.has(SWAP_CLASS), false);
  assert.equal(ghostsOf(btn).length, 0);
});

test('rapid toggling: ghosts never stack, and a stale timer cannot strip a newer swap', () => {
  const btn = makeBtn();
  const t = timerBox();
  const face = (on) => swapContent(btn, `<span>${on ? 'Stop' : 'Start'}</span>`,
    { key: on ? 'stop' : 'start', setTimer: t.setTimer });
  face(false);
  for (let i = 0; i < 12; i++) {
    face(i % 2 === 1);
    assert.ok(ghostsOf(btn).length <= 1, 'at most one ghost is ever in flight');
  }
  // The 11 superseded timers fire late — the newest swap must survive them...
  assert.equal(btn.classes.has(SWAP_CLASS), true);
  t.flush();
  // ...and once the last one has run, nothing is left playing or hanging around.
  assert.equal(btn.innerHTML, '<span>Stop</span>', 'the final face is the last one asked for');
  assert.equal(btn.classes.has(SWAP_CLASS), false, 'no stuck animation');
  assert.equal(ghostsOf(btn).length, 0, 'no leftover ghost');
});

test('swapContent survives a bare element (no ghost to make, no throw)', () => {
  const t = timerBox();
  const bare = { innerHTML: '', classList: { add() {}, remove() {} } };
  assert.doesNotThrow(() => {
    swapContent(bare, 'a', { key: 'a', setTimer: t.setTimer });
    swapContent(bare, 'b', { key: 'b', setTimer: t.setTimer });
  });
  assert.equal(bare.innerHTML, 'b');
  assert.equal(SWAP_MS > 0, true);
});

// ── 4. The real toggles: DrawingApp.syncDrawToggleUI / syncDrawModeUI ───────

// A fresh pair of registered buttons and a `this` with only what the two methods touch.
// Fresh elements per test: the swap's "same face" skip is keyed by element, so a reused
// button would carry the previous test's face.
const uiRig = () => {
  const drawBtn = makeBtn();
  const modeBtn = makeBtn();
  doc.register('draw-toggle', drawBtn);
  doc.register('draw-mode-toggle', modeBtn);
  const app = {
    isDrawing: false,
    drawMode: 'line',
    syncDrawToggleUI: DrawingApp.prototype.syncDrawToggleUI,
    syncDrawModeUI: DrawingApp.prototype.syncDrawModeUI,
    setDrawMode: DrawingApp.prototype.setDrawMode,
  };
  return { app, drawBtn, modeBtn };
};
// The visible face, as (glyph, label) — one label or the assertion fails loudly.
const faceOf = (btn) => ({
  glyph: /ic-stop/.test(btn.innerHTML) ? 'stop'
    : /ic-play/.test(btn.innerHTML) ? 'play'
      : /<rect /.test(btn.innerHTML) ? 'rect'
        : /<line /.test(btn.innerHTML) ? 'line' : '?',
  labels: [...btn.innerHTML.matchAll(/<span>([^<]*)<\/span>/g)].map((m) => m[1]),
});

test('Start/Stop: the glyph, the word and the accent fill all follow isDrawing', () => {
  const { app, drawBtn } = uiRig();
  app.syncDrawToggleUI();
  assert.deepEqual(faceOf(drawBtn), { glyph: 'play', labels: ['Start'] });
  assert.equal(drawBtn.classes.has('active'), false, 'idle is the OUTLINED state');

  app.isDrawing = true;
  app.syncDrawToggleUI();
  assert.deepEqual(faceOf(drawBtn), { glyph: 'stop', labels: ['Stop'] });
  assert.equal(drawBtn.classes.has('active'), true, 'drawing is the accent-FILLED state');
});

test('Start/Stop: the swap keeps the tooltip and its state-dependent hotkey', () => {
  const { app, drawBtn } = uiRig();
  app.syncDrawToggleUI();
  assert.equal(drawBtn.dataset.hkTitle, 'startDraw');
  assert.equal(drawBtn.dataset.title, 'Start Drawing');
  assert.ok(drawBtn.dataset.tip.startsWith('Start Drawing'), `composed tip, got ${drawBtn.dataset.tip}`);

  app.isDrawing = true;
  app.syncDrawToggleUI();
  assert.equal(drawBtn.dataset.hkTitle, 'stopDraw', 'Alt+A starts, Alt+S stops');
  assert.equal(drawBtn.dataset.title, 'Stop Drawing');
  assert.ok(drawBtn.dataset.tip.startsWith('Stop Drawing'), `composed tip, got ${drawBtn.dataset.tip}`);
});

test('Line/Rect: the mode toggle swaps glyph, word and tooltip', () => {
  const { app, modeBtn } = uiRig();
  app.syncDrawModeUI();
  assert.deepEqual(faceOf(modeBtn), { glyph: 'line', labels: ['Line'] });
  assert.match(modeBtn.dataset.title, /^Drawing mode: Line/);

  app.setDrawMode('rect');
  assert.deepEqual(faceOf(modeBtn), { glyph: 'rect', labels: ['Rect'] });
  assert.match(modeBtn.dataset.title, /^Drawing mode: Rectangle/);
});

test('holding the hotkey: every face still matches the state it was rendered for', () => {
  const { app, drawBtn, modeBtn } = uiRig();
  // 40 alternations with no timer ever firing — the animation is mid-flight throughout.
  for (let i = 0; i < 40; i++) {
    app.isDrawing = i % 2 === 0;
    app.syncDrawToggleUI();
    app.setDrawMode(i % 2 === 0 ? 'rect' : 'line');
    assert.deepEqual(faceOf(drawBtn),
      { glyph: app.isDrawing ? 'stop' : 'play', labels: [app.isDrawing ? 'Stop' : 'Start'] },
      `stale or doubled face after toggle ${i}`);
    assert.equal(drawBtn.classes.has('active'), app.isDrawing);
    assert.deepEqual(faceOf(modeBtn),
      { glyph: app.drawMode, labels: [app.drawMode === 'rect' ? 'Rect' : 'Line'] });
    assert.ok(ghostsOf(drawBtn).length <= 1, 'ghosts never stack under a held key');
    assert.ok(ghostsOf(modeBtn).length <= 1);
  }
});

test('the swap is decoration: updateButtons re-syncing does not replay it', () => {
  const { app, drawBtn } = uiRig();
  app.syncDrawToggleUI();
  app.isDrawing = true;
  app.syncDrawToggleUI();
  const ghosts = ghostsOf(drawBtn).length;
  for (let i = 0; i < 20; i++) app.syncDrawToggleUI();   // every redraw calls this
  assert.equal(ghostsOf(drawBtn).length, ghosts, 'an unchanged face starts nothing new');
  assert.deepEqual(faceOf(drawBtn), { glyph: 'stop', labels: ['Stop'] });
});

test('updateButtons seeds BOTH faces, so the session’s first Line↔Rect switch animates', () => {
  const src = read('../js/ui/controlState.js');
  assert.match(src, /app\.syncDrawToggleUI\(\);[\s\S]{0,400}?app\.syncDrawModeUI\(\);/,
    'the Draw group’s two faces are owned by one place');
  // Without the seed the first swapContent call on the mode button IS its first paint,
  // which deliberately does not animate — the switch would silently jump once per session.
  const { app, modeBtn } = uiRig();
  app.syncDrawModeUI();                       // the boot seed
  assert.equal(modeBtn.classes.has(SWAP_CLASS), false, 'the seed itself never animates');
  app.setDrawMode('rect');                    // the user's first switch
  assert.equal(modeBtn.classes.has(SWAP_CLASS), true, 'and that one does');
});

test('the sync methods write the face through the shared swap, not innerHTML (source pin)', () => {
  const src = read('../js/core/drawingApp.js');
  const body = src.slice(src.indexOf('syncDrawToggleUI() {'), src.indexOf('stopDrawingMode() {'));
  assert.ok(!/btn\.innerHTML\s*=/.test(body),
    'a raw innerHTML write cannot animate — that is what the green-button bug fix replaced');
  assert.equal((body.match(/swapContent\(/g) || []).length, 2, 'both toggles share one transition');
});

// ── 3. The two faces are a matched SET ──────────────────────────────────────
// Line and Rect are one toggle, so they must read as siblings: same box, same stroke,
// same two handles — (3,13) and (13,3), the corners a drag starts and ends on — with
// only the shape joining them different. Before this they were different families: the
// line carried endpoint dots, the rect was a bare outline with none.
test('the Line and Rect glyphs are siblings, not two different families', async () => {
  const { DRAW_MODE_ICON } = await import('../js/ui/icons.js');
  const svgs = [DRAW_MODE_ICON.line, DRAW_MODE_ICON.rect];
  for (const [name, svg] of Object.entries(DRAW_MODE_ICON)) {
    assert.match(svg, /class="draw-mode-icon"/, `${name} keeps the class the swap targets`);
    assert.match(svg, /width="13" height="13"/, `${name} stays 13px`);
    assert.match(svg, /viewBox="0 0 16 16"/, `${name} shares the grid`);
    assert.ok(!/#|rgb\(|var\(/.test(svg), `${name} paints in currentColor only`);
    // The same two handles, filled, at the same radius.
    assert.match(svg, /<circle class="ic-handle" cx="3" cy="13" r="2" fill="currentColor"\/>/, `${name} anchors the start handle`);
    assert.match(svg, /<circle class="ic-handle" cx="13" cy="3" r="2" fill="currentColor"\/>/, `${name} anchors the end handle`);
  }
  // One stroke weight across the pair — a heavier rect would read as a different set.
  const weights = new Set(svgs.flatMap((s) => [...s.matchAll(/stroke-width="([^"]+)"/g)].map((m) => m[1])));
  assert.deepEqual([...weights], ['1.5'], 'one stroke weight across both faces');
  // …and the rect really spans those two handles (3,3)→(13,13), so the dots sit ON it.
  assert.match(DRAW_MODE_ICON.rect, /<rect class="ic-box" x="3" y="3" width="10" height="10"/,
    'the box spans the same corners the line connects');
});

// The pair also exists in the CANONICAL set now (config/icons.json `line` / `rect`), where
// the desktop and the extension read it: the toolbar's inline 16-grid pair is the same
// drawing on the shared 24-grid, scaled x1.5. The desktop paired a pencil with a solid
// slab before it landed, which is exactly the drift this pins.
test('the canonical line/rect pair is the inline pair, scaled onto the 24-grid', async () => {
  const { DRAW_MODE_ICON } = await import('../js/ui/icons.js');
  const ICONS = JSON.parse(readFileSync(new URL('../js/config/icons.json', import.meta.url), 'utf8'));
  // Every geometry number in the inline face, x1.5 (16-grid → 24-grid).
  const scaled = (svg, attrs) => attrs.map((a) => {
    const v = svg.match(new RegExp(`${a}="([\\d.]+)"`));
    return v ? +(parseFloat(v[1]) * 1.5).toString() : null;
  });
  assert.deepEqual(scaled(DRAW_MODE_ICON.line, ['x1', 'y1', 'x2', 'y2']), [4.5, 19.5, 19.5, 4.5]);
  assert.match(ICONS.line, /<line class="ic-stroke" x1="4.5" y1="19.5" x2="19.5" y2="4.5"\/>/);
  // …from the <rect> element itself: the <svg> wrapper carries a width="13" of its own.
  assert.deepEqual(scaled(DRAW_MODE_ICON.rect.match(/<rect[^>]*>/)[0], ['x', 'y', 'width', 'height']),
    [4.5, 4.5, 15, 15]);
  assert.match(ICONS.rect, /<rect class="ic-box" x="4.5" y="4.5" width="15" height="15" rx="1.5"\/>/);
  // The same two handles, on the same corners, at the scaled radius.
  for (const glyph of [ICONS.line, ICONS.rect]) {
    assert.match(glyph, /<circle class="ic-handle" cx="4.5" cy="19.5" r="3" fill="currentColor" stroke="none"\/>/);
    assert.match(glyph, /<circle class="ic-handle" cx="19.5" cy="4.5" r="3" fill="currentColor" stroke="none"\/>/);
  }
});

// REGRESSION: with the rect tool picked, pressing the mouse did nothing — the sweep
// required drawing mode to be on already, and rect has no hold-to-draw flow to fall back
// on. Picking the tool is the intent, so the press turns drawing on itself.
test('the rect tool starts its sweep on the press, turning drawing on by itself', () => {
    const js = readFileSync(new URL('../js/core/pointerController.js', import.meta.url), 'utf8');
    const branch = js.slice(js.indexOf('// Rect-draw mode:'), js.indexOf("// Alt+Ctrl/⌘+left"));
    // The gate no longer demands isDrawing…
    assert.ok(!/if \(app\.isDrawing && app\.drawMode === 'rect'/.test(branch),
        'the sweep must not require drawing mode to be on already');
    assert.match(branch, /if \(app\.drawMode === 'rect' && e\.button === 0/);
    // …it turns it on, and gives up cleanly if that is declined (no image / read-only).
    assert.match(branch, /if \(!app\.isDrawing\) app\.startDrawingMode\(\);/);
    assert.match(branch, /if \(!app\.isDrawing\) return;/);
    assert.ok(branch.indexOf('startDrawingMode') < branch.indexOf('isRectDrawDragging = true'),
        'drawing goes on before the band starts');
    // The desktop press does the same, so the two tools behave alike.
    const cpp = readFileSync(new URL('../../desktop/src/canvas/canvasWidget.cpp', import.meta.url), 'utf8');
    const dbranch = cpp.slice(cpp.indexOf('// rect-draw press'), cpp.indexOf('// when not drawing, a left-click'));
    assert.match(dbranch, /if \(drawMode_ == DrawMode::Rect && mods == Qt::NoModifier\)/);
    assert.match(dbranch, /if \(!isDrawing_\) startDrawingMode\(\);/);
});

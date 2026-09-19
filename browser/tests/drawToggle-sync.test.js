// DrawingApp.syncDrawToggleUI / syncDrawModeUI: the real toggles' faces, tooltips and accent
// fill, driven on a stub document. Split from drawToggle.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { installDom, createStubElement } from './helpers/dom.js';
import { SWAP_CLASS, SWAP_GHOST_CLASS } from '../js/ui/motion.js';

const doc = installDom({}, { location: { hash: '', pathname: '/app', search: '' }, history: { replaceState: () => {} } });
const { DrawingApp } = await import('../js/core/drawingApp.js');

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const makeBtn = () => {
  const el = createStubElement('button');
  el.querySelectorAll = () => el.children.filter((c) => c.classes?.has?.(SWAP_GHOST_CLASS));
  return el;
};
const ghostsOf = (el) => el.children.filter((c) => c.classes?.has?.(SWAP_GHOST_CLASS));

// A `this` with only what the two methods touch, and fresh elements per test: the swap's "same face"
// skip is keyed by element, so a reused button would carry the previous test's face.
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
  const body = read('../js/ui/drawToggleUI.js');
  assert.ok(!/btn\.innerHTML\s*=/.test(body),
    'a raw innerHTML write cannot animate — that is what the green-button bug fix replaced');
  assert.equal((body.match(/swapContent\(/g) || []).length, 2, 'both toggles share one transition');
});

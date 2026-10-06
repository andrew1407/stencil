// syncDrawToggleUI / syncDrawModeUI (ui/panel/drawToggleUI.js): the real toggles' faces, tooltips
// and accent fill, driven on a stub document. Split from drawToggle.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';
import { SWAP_CLASS, SWAP_GHOST_CLASS } from '../../../js/ui/motion.js';

const doc = installDom({}, { location: { hash: '', pathname: '/app', search: '' }, history: { replaceState: () => {} } });
const { DrawingApp } = await import('../../../js/core/drawingApp.js');
const { updateButtons } = await import('../../../js/ui/control/state.js');
const { syncDrawToggleUI, syncDrawModeUI } = await import('../../../js/ui/panel/drawToggleUI.js');
const { setDrawMode } = await import('../../../js/core/draw/mode.js');
const { makeControlApp } = await import('../../helpers/controlStateRig.js');
const makeBtn = () => {
  const el = createStubElement('button');
  el.querySelectorAll = () => el.children.filter((c) => c.classes?.has?.(SWAP_GHOST_CLASS));
  return el;
};
const ghostsOf = (el) => el.children.filter((c) => c.classes?.has?.(SWAP_GHOST_CLASS));

// An app with only what the two faces touch — setDrawMode reaches the mode face through the app's
// view seam — and fresh elements per test: the swap's "same face" skip is keyed by element, so a
// reused button would carry the previous test's face.
const uiRig = () => {
  const drawBtn = makeBtn();
  const modeBtn = makeBtn();
  doc.register('draw-toggle', drawBtn);
  doc.register('draw-mode-toggle', modeBtn);
  const app = {
    isDrawing: false,
    drawMode: 'line',
    syncDrawModeUI: DrawingApp.prototype.syncDrawModeUI,
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
  syncDrawToggleUI(app);
  assert.deepEqual(faceOf(drawBtn), { glyph: 'play', labels: ['Start'] });
  assert.equal(drawBtn.classes.has('active'), false, 'idle is the OUTLINED state');

  app.isDrawing = true;
  syncDrawToggleUI(app);
  assert.deepEqual(faceOf(drawBtn), { glyph: 'stop', labels: ['Stop'] });
  assert.equal(drawBtn.classes.has('active'), true, 'drawing is the accent-FILLED state');
});

test('Start/Stop: the swap keeps the tooltip and its one toggle hotkey', () => {
  const { app, drawBtn } = uiRig();
  syncDrawToggleUI(app);
  assert.equal(drawBtn.dataset.hkTitle, 'startDraw');
  assert.equal(drawBtn.dataset.title, 'Start Drawing');
  assert.ok(drawBtn.dataset.tip.startsWith('Start Drawing'), `composed tip, got ${drawBtn.dataset.tip}`);

  app.isDrawing = true;
  syncDrawToggleUI(app);
  assert.equal(drawBtn.dataset.hkTitle, 'startDraw', 'Alt+A both starts and stops');
  assert.equal(drawBtn.dataset.title, 'Stop Drawing');
  assert.ok(drawBtn.dataset.tip.startsWith('Stop Drawing'), `composed tip, got ${drawBtn.dataset.tip}`);
});

test('Line/Rect: the mode toggle swaps glyph, word and tooltip', () => {
  const { app, modeBtn } = uiRig();
  syncDrawModeUI(app);
  assert.deepEqual(faceOf(modeBtn), { glyph: 'line', labels: ['Line'] });
  assert.match(modeBtn.dataset.title, /^Drawing mode: Line/);

  setDrawMode(app, 'rect');
  assert.deepEqual(faceOf(modeBtn), { glyph: 'rect', labels: ['Rect'] });
  assert.match(modeBtn.dataset.title, /^Drawing mode: Rectangle/);
});

test('holding the hotkey: every face still matches the state it was rendered for', () => {
  const { app, drawBtn, modeBtn } = uiRig();
  // 40 alternations with no timer ever firing — the animation is mid-flight throughout.
  for (let i = 0; i < 40; i++) {
    app.isDrawing = i % 2 === 0;
    syncDrawToggleUI(app);
    setDrawMode(app, i % 2 === 0 ? 'rect' : 'line');
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
  syncDrawToggleUI(app);
  app.isDrawing = true;
  syncDrawToggleUI(app);
  const ghosts = ghostsOf(drawBtn).length;
  for (let i = 0; i < 20; i++) syncDrawToggleUI(app);   // every redraw calls this
  assert.equal(ghostsOf(drawBtn).length, ghosts, 'an unchanged face starts nothing new');
  assert.deepEqual(faceOf(drawBtn), { glyph: 'stop', labels: ['Stop'] });
});

test('updateButtons seeds BOTH faces, so the session’s first Line↔Rect switch animates', () => {
  // Without the seed the first swapContent call on the mode button IS its first paint,
  // which deliberately does not animate — the switch would silently jump once per session.
  const { app, drawBtn, modeBtn } = uiRig();
  const byId = doc.getElementById;
  doc.getElementById = (id) => byId(id) ?? doc.register(id, createStubElement('div'));
  try {
    updateButtons(Object.assign(app, makeControlApp({ canvas: null })));
  } finally {
    doc.getElementById = byId;
  }
  assert.deepEqual([faceOf(drawBtn), faceOf(modeBtn)], [{ glyph: 'play', labels: ['Start'] }, { glyph: 'line', labels: ['Line'] }],
    'the Draw group’s two faces are owned by one place');
  assert.equal(modeBtn.classes.has(SWAP_CLASS), false, 'the seed itself never animates');
  setDrawMode(app, 'rect');                    // the user's first switch
  assert.equal(modeBtn.classes.has(SWAP_CLASS), true, 'and that one does');
});

test('the sync methods write the face through the shared swap, not a raw innerHTML', () => {
  const { app, drawBtn, modeBtn } = uiRig();
  syncDrawToggleUI(app);
  syncDrawModeUI(app);
  const before = [drawBtn.innerHTML, modeBtn.innerHTML];
  app.isDrawing = true;
  syncDrawToggleUI(app);
  setDrawMode(app, 'rect');
  // A raw write cannot animate: the swap stacks the old face as a ghost over the new one.
  for (const [btn, old] of [[drawBtn, before[0]], [modeBtn, before[1]]]) {
    assert.equal(btn.classes.has(SWAP_CLASS), true, 'both toggles share one transition');
    assert.deepEqual(ghostsOf(btn).map((g) => g.innerHTML), [old], 'the leaving face rides the ghost');
  }
});

// A variant or ask preview rewound through the REAL editorHistory adapter: the undo stack, its
// cursor, its floor and the view come back as they were, so a preview leaves no undo step.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { HistoryStack, editorMemento } from '../../../js/core/historyStack.js';
import { rotateEditQuarter, snapCropRect, cropChange, scaleLinePoints } from '../../../js/core/parse/cropGeometry.js';
import { editorAdapters } from '../../../js/llm/adapters/editor.js';
import { executeOpPlan, parseOpPlan, renderAskPreviews } from '../../../js/llm/plan/opPlan.js';
import { captureEditorState } from '../../../js/llm/plan/sandbox.js';

// An app with the real history and the model's own turn/crop maths, each edit one memento pushed.
const makeEditor = (w = 8, h = 6) => {
  const calls = [];
  const app = {
    calls, originalImage: { width: w, height: h }, rotationQuarters: 0, cropRect: { x: 0, y: 0, width: w, height: h },
    lines: [], history: new HistoryStack(), filter: 'none', filterColor: '#7c3aed', pageSize: 'A4',
    renderer: { redraw: () => calls.push('redraw') }, updateButtons: () => calls.push('buttons'),
    coordTable: { update: () => {} }, storage: { saveSoon: () => calls.push('save') },
    remoteSync: { scheduleRemoteSync: () => calls.push('sync') },
  };
  const push = () => app.history.push(editorMemento(app));
  app.restoreHistoryStep = (step) => {
    const m = Array.isArray(step) ? { lines: step } : step;
    app.lines = m.lines;
    if (m.cropRect) { app.cropRect = { ...m.cropRect }; app.rotationQuarters = m.rotationQuarters ?? 0; }
  };
  const turn = (cw) => {
    const t = rotateEditQuarter(app.lines, app.cropRect, app.rotationQuarters, w, h, cw);
    app.rotationQuarters = t.quarters;
    app.cropRect = t.crop;
    push();
  };
  const dims = () => (app.rotationQuarters % 2 ? { w: h, h: w } : { w, h });
  const tok = (t, cur, len) => (t == null ? cur : /%$/.test(t) ? (parseFloat(t) / 100) * len : parseFloat(t));
  const stencil = {
    get imageSize() { return { width: app.cropRect.width, height: app.cropRect.height }; },
    get cropRect() { const r = app.cropRect; return { x: r.x, y: r.y, w: r.width, h: r.height, width: r.width, height: r.height }; },
    get lines() { return app.lines; },
    get filter() { return app.filter; }, get filterColor() { return app.filterColor; }, get pageSize() { return app.pageSize; },
    allowFormulas: false, formulaX: '', formulaY: '',
    rotateLeft() { turn(false); return stencil; },
    rotateRight() { turn(true); return stencil; },
    crop(spec) {
      const d = dims(), r = app.cropRect;
      const x1 = tok(spec.x1, r.x, d.w), x2 = tok(spec.x2, r.x + r.width, d.w);
      const y1 = tok(spec.y1, r.y, d.h), y2 = tok(spec.y2, r.y + r.height, d.h);
      const next = snapCropRect({ x: Math.min(x1, x2), y: Math.min(y1, y2), width: Math.abs(x2 - x1), height: Math.abs(y2 - y1) }, d.w, d.h);
      const change = cropChange(app.cropRect, next);
      if (change.orientationChanged) app.lines = []; else if (change.scale !== 1) scaleLinePoints(app.lines, change.scale);
      app.cropRect = next;
      push();
      return stencil;
    },
    apply(o) { for (const k of ['filter', 'filterColor', 'pageSize']) if (o[k] != null) app[k] = o[k]; return stencil; },
    setLines(lines, opts = {}) { app.lines = structuredClone(lines); if (opts.history !== false) push(); return stencil; },
    undo() { const r = app.history.undo(); if (r !== null) app.restoreHistoryStep(r); return stencil; },
    redo() { const r = app.history.redo(); if (r !== null) app.restoreHistoryStep(r); return stencil; },
  };
  app.history.reset(editorMemento(app));
  return { app, stencil };
};

const LINES = [{ points: [{ x: 1, y: 2 }, { x: 5, y: 3 }], color: '#00ff00' }];
const view = (app) => JSON.stringify({ lines: app.lines, crop: app.cropRect, q: app.rotationQuarters, filter: app.filter });
const stack = (app) => JSON.stringify({ h: app.history.history, step: app.history.historyStep, floor: app.history.floor });
// Raw ask actions skip the parser's defaults, so `times` is spelled out.
const TURN_AND_CROP = [{ op: 'rotate', dir: 'right', times: 1 }, { op: 'crop', spec: { x1: '1px', x2: '5px', y1: '1px', y2: '6px' } }];

// The user's own work: lines drawn, then a crop — two real undo steps.
const edited = () => {
  const ed = makeEditor();
  ed.stencil.setLines(LINES);
  ed.stencil.crop({ x1: '1px', x2: '7px', y1: '1px', y2: '5px' });
  return ed;
};
const opts = (ed) => ({ exportImage: async () => view(ed.app), editorHistory: editorAdapters(ed.app).editorHistory });

test('an ask preview that turns and crops leaves the stack, its cursor and the view as they were', async () => {
  const ed = edited();
  const beforeView = view(ed.app), beforeStack = stack(ed.app);
  const ask = { question: 'Which?', options: [{ label: 'turned', actions: TURN_AND_CROP }] };
  const { previews } = await renderAskPreviews(ask, ed.stencil, opts(ed));
  assert.equal(previews.length, 1);
  assert.notEqual(previews[0].dataUrl, beforeView, 'the preview rendered the turned picture');
  assert.equal(view(ed.app), beforeView);
  assert.equal(stack(ed.app), beforeStack);
  assert.equal(ed.app.history.canRedo(), false, 'no redo branch into the preview');
  assert.deepEqual(ed.app.calls.slice(-4), ['redraw', 'buttons', 'save', 'sync'], 'repainted and persisted');
});

test('one undo after a preview undoes the user\'s last real edit', async () => {
  const ed = edited();
  const ref = makeEditor();
  ref.stencil.setLines(LINES);
  const ask = { question: 'Which?', options: [{ label: 'a', actions: TURN_AND_CROP }, { label: 'b', actions: [{ op: 'rotate', dir: 'left', times: 3 }] }] };
  await renderAskPreviews(ask, ed.stencil, opts(ed));
  ed.stencil.undo();
  assert.equal(view(ed.app), view(ref.app), 'back to the lines before the crop, unturned');
  ed.stencil.undo();
  assert.equal(ed.app.lines.length, 0);
  assert.equal(ed.app.history.canUndo(), false);
});

test('variants rewind the same way, and a redo branch the user had survives them', async () => {
  const ed = edited();
  ed.stencil.undo();
  const beforeView = view(ed.app), beforeStack = stack(ed.app);
  const variants = [{ label: 'v1', actions: TURN_AND_CROP }, { label: 'v2', actions: [{ op: 'rotate', dir: 'right', times: 2 }] }];
  const out = await executeOpPlan(parseOpPlan(JSON.stringify({ version: 1, reply: 'ok', actions: [], variants })), ed.stencil, opts(ed));
  assert.equal(out.results.length, 2);
  assert.equal(view(ed.app), beforeView);
  assert.equal(stack(ed.app), beforeStack);
  ed.stencil.redo();
  assert.deepEqual(ed.app.cropRect, { x: 1, y: 1, width: 6, height: 4 }, 'redo still re-applies the user\'s crop');
});

test('a preview that throws after turning is rewound all the same', async () => {
  const ed = edited();
  const beforeView = view(ed.app), beforeStack = stack(ed.app);
  const ask = { question: 'Which?', options: [{ label: 'x', actions: [{ op: 'rotate', dir: 'left', times: 1 }, { op: 'frame', index: 2 }] }] };
  const { previews, warnings } = await renderAskPreviews(ask, ed.stencil, opts(ed));
  assert.equal(previews.length, 0);
  assert.equal(warnings.length, 1);
  assert.equal(view(ed.app), beforeView);
  assert.equal(stack(ed.app), beforeStack);
});

test('with the history capability the snapshot names the quarter turn on screen', () => {
  const ed = edited();
  ed.stencil.rotateRight();
  const state = captureEditorState(ed.stencil, opts(ed).editorHistory);
  assert.equal(state.rotationQuarters, 1);
  assert.deepEqual(state.cropRect, ed.app.cropRect);
  assert.equal(state.mark.memento.rotationQuarters, 1, 'the turn is the mark\'s own');
});

test('a preview that tints and turns is rewound, the filter with it, leaving no step', async () => {
  const ed = edited();
  const beforeView = view(ed.app), beforeStack = stack(ed.app);
  const ask = { question: 'Which?', options: [{ label: 'sepia', actions: [{ op: 'filter', mode: 'sepia' }, ...TURN_AND_CROP] }] };
  const { previews } = await renderAskPreviews(ask, ed.stencil, opts(ed));
  assert.equal(previews.length, 1);
  assert.match(previews[0].dataUrl, /"filter":"sepia"/, 'the preview rendered the tint');
  assert.equal(view(ed.app), beforeView);
  assert.equal(stack(ed.app), beforeStack);
});

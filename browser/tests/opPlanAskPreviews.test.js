// §11 renderAskPreviews (js/llm/plan.js) against a recording facade: one picture per
// actions-bearing option, and the working image restored after every one.
import { test } from 'node:test';
import assert from 'node:assert';
import { renderAskPreviews } from '../js/llm/plan/opPlan.js';
import { askOf } from './helpers/opPlanRig.js';

// §11 renderAskPreviews, against a mock facade recording what the executor did: every preview
// must leave the working image exactly as it found it.
const previewStencil = () => {
  const calls = [];
  let current = 'base.png', rot = 0, cropped = false;   // net clockwise turns; off the base rect
  const st = {
    calls,
    filter: 'none', filterColor: '#7c3aed', pageSize: 'A4',
    allowFormulas: false, formulaX: '', formulaY: '',
    showPoints: true, showLines: true,
    lines: [],
    // What an export shows: the pixels PLUS the active filter, like the real renderer —
    // so a filter is visible in the render without being burnt into the pixels.
    get loaded() { return current + ({ 1: '+rotR', 2: '+rot2', 3: '+rotL' }[rot] || '') + (cropped ? '+crop' : '') + (st.filter !== 'none' ? '+filter' : ''); },
    get cropRect() { return cropped ? { x: 5, y: 5, w: 50, h: 40 } : { x: 0, y: 0, w: 100, h: 80 }; },
    rotateLeft() { calls.push('rotateLeft'); rot = (rot + 3) % 4; return st; },
    rotateRight() { calls.push('rotateRight'); rot = (rot + 1) % 4; return st; },
    apply(o) {
      calls.push(`apply:${JSON.stringify(o)}`);
      for (const k of ['filter', 'filterColor', 'pageSize', 'allowFormulas', 'formulaX', 'formulaY', 'showPoints', 'showLines'])
        if (o[k] != null) st[k] = o[k];
      if (o.page != null) st.pageSize = o.page;
      return st;
    },
    crop(spec) { calls.push('crop'); cropped = spec.x1 !== '0px'; return st; },
    async load(url) { calls.push(`load:${url}`); current = url; rot = 0; cropped = false; st.lines = []; return st; },
    setLines(lines) { calls.push('setLines'); st.lines = lines || []; return st; },
    get imageSize() { return { width: 100, height: 80 }; },
  };
  Object.defineProperty(st, 'layout', { set(v) { calls.push('layout'); st.lines = (v && v.lines) || []; } });
  return st;
};

test('renderAskPreviews: renders one picture per actions-bearing option, restoring the image each time', async () => {
  const st = previewStencil();
  const ask = askOf({ question: 'Which?', options: [
    { label: 'Left', actions: [{ op: 'rotate', dir: 'left' }] },
    { label: 'Sepia', actions: [{ op: 'filter', mode: 'sepia' }] },
  ] });
  const exportImage = async () => `shot(${st.loaded})`;
  const { previews, warnings } = await renderAskPreviews(ask, st, { exportImage });
  assert.deepEqual(warnings, []);
  assert.deepEqual(previews.map((p) => p.index), [0, 1]);
  assert.deepEqual(previews.map((p) => p.label), ['Left', 'Sepia']);
  // Each preview is a shot of the base with ONLY its own action applied — the second is not
  // rotated, so the options don't stack on each other.
  assert.match(previews[0].dataUrl, /\+rotL\)$/);
  assert.match(previews[1].dataUrl, /\+filter\)$/);
  assert.ok(!previews[1].dataUrl.includes('rotL'));
  // The working image is back to its base — a preview never edits, and never reloads:
  // the rotate is turned back on the model, the filter undone by putting the setting back.
  assert.equal(st.loaded, 'base.png');
  assert.equal(st.calls.filter((c) => c.startsWith('load:')).length, 0);
  assert.deepEqual(st.calls.filter((c) => /^rotate/.test(c)), ['rotateLeft', 'rotateRight']);
  assert.equal(st.filter, 'none', 'the previewed filter must not stick');
});

test('renderAskPreviews: options with a reference or nothing get no picture (the surface resolves those)', async () => {
  const st = previewStencil();
  const ask = askOf({ question: 'Which?', options: [
    { label: 'Web', image: { url: 'https://example.com/a.png' } },
    { label: 'Plain' },
  ] });
  const { previews, warnings } = await renderAskPreviews(ask, st, { exportImage: async () => 'x' });
  assert.deepEqual(previews, []);
  assert.deepEqual(warnings, []);
  assert.deepEqual(st.calls, []);            // nothing rendered → the image is never touched
});

test('renderAskPreviews: one failing option loses its picture, not the card — and still restores', async () => {
  const st = previewStencil();
  const ask = askOf({ question: 'Which frame?', options: [
    { label: '0:04', actions: [{ op: 'frame', index: 120 }] },   // no loadFrame → not a video
    { label: 'Left', actions: [{ op: 'rotate', dir: 'left' }] },
  ] });
  const { previews, warnings } = await renderAskPreviews(ask, st, { exportImage: async () => `shot(${st.loaded})` });
  assert.deepEqual(previews.map((p) => p.label), ['Left']);      // the good one still rendered
  assert.equal(warnings.length, 1);
  assert.match(warnings[0], /Could not preview "0:04"/);
  assert.equal(st.loaded, 'base.png');                           // restored despite the throw
});

test('renderAskPreviews: a frame preview works when the surface can decode video', async () => {
  const st = previewStencil();
  const seen = [];
  const ask = askOf({ question: 'Which frame?', options: [
    { label: '0:04', actions: [{ op: 'frame', index: 120 }] },
    { label: '0:07', actions: [{ op: 'frame', index: 210 }] },
  ] });
  const { previews, warnings } = await renderAskPreviews(ask, st, {
    exportImage: async () => `frame(${seen[seen.length - 1] ?? 'base'})`,
    loadFrame: async (i) => { seen.push(i); },
  });
  assert.deepEqual(warnings, []);
  assert.deepEqual(seen, [120, 210]);
  assert.deepEqual(previews.map((p) => p.dataUrl), ['frame(120)', 'frame(210)']);
});

test('renderAskPreviews: no ask, no exporter, no options → nothing, and no throw', async () => {
  assert.deepEqual((await renderAskPreviews(null, previewStencil(), { exportImage: async () => 'x' })).previews, []);
  const ask = askOf({ question: 'Q', options: [{ label: 'A', actions: [{ op: 'rotate', dir: 'left' }] }, { label: 'B' }] });
  assert.deepEqual((await renderAskPreviews(ask, previewStencil(), {})).previews, []);   // no exportImage
});

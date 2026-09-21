import { test } from 'node:test';
import assert from 'node:assert/strict';
import { executeOpPlan, parseOpPlan, renderAskPreviews } from '../js/llm/plan/opPlan.js';
import { captureEditorState, capturePixels, needsPixelSnapshot, restoreWorkingImage } from '../js/llm/plan/planSandbox.js';
import { rotateCropRectQuarterJS, rotateLinePointsQuarter, cropChangeJS, scaleLinePoints } from '../js/core/parse/cropGeometry.js';

// A facade stub with REAL pixels: an RGBA original, the editor's own quarter-turn and
// crop maths (cropGeometry), and an export that bakes the filter + lines in like the
// renderer. Enough to compare what two restore strategies leave behind, byte for byte.
const plan = (over) => JSON.stringify({ version: 1, reply: 'ok', actions: [], variants: [], ...over });

const rotateCW = ({ w, h, data }) => {
  const out = new Uint8Array(data.length);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const nx = h - 1 - y, ny = x;
    out.set(data.subarray((y * w + x) * 4, (y * w + x) * 4 + 4), (ny * h + nx) * 4);
  }
  return { w: h, h: w, data: out };
};

const makeStencil = (w = 8, h = 6, state = {}) => {
  const original = { w, h, data: Uint8Array.from({ length: w * h * 4 }, (_, i) => (i * 37) % 251) };
  const calls = [];
  const st = {
    calls, original, q: 0, cropRect: { x: 0, y: 0, width: w, height: h }, lines: [],
    filter: 'none', filterColor: '#7c3aed', pageSize: 'A4', allowFormulas: false, formulaX: '', formulaY: '',
    showPoints: true, showLines: true, ...state,
  };
  const rotated = () => { let img = st.original; for (let i = 0; i < st.q; i++) img = rotateCW(img); return img; };
  const roundRect = (r, iw, ih) => {
    const rw = Math.max(1, Math.min(Math.round(r.width), iw)), rh = Math.max(1, Math.min(Math.round(r.height), ih));
    return { x: Math.max(0, Math.min(Math.round(r.x), iw - rw)), y: Math.max(0, Math.min(Math.round(r.y), ih - rh)), width: rw, height: rh };
  };
  const dims = () => (st.q % 2 ? { w: st.original.h, h: st.original.w } : { w: st.original.w, h: st.original.h });
  st.working = () => {
    const src = rotated(), r = st.cropRect, out = new Uint8Array(r.width * r.height * 4);
    for (let y = 0; y < r.height; y++) out.set(src.data.subarray(((r.y + y) * src.w + r.x) * 4, ((r.y + y) * src.w + r.x + r.width) * 4), y * r.width * 4);
    return out;
  };
  Object.defineProperty(st, 'imageSize', { get: () => ({ width: st.cropRect.width, height: st.cropRect.height }) });
  // The "render": pixels + what the renderer paints over them (filter tint, lines).
  st.exportImage = async () => JSON.stringify({ w: st.cropRect.width, h: st.cropRect.height, px: [...st.working()],
    filter: st.filter, lines: st.showLines ? st.lines : [] });
  const tok = (t, cur, len) => (t == null ? cur : typeof t === 'number' ? cur + t : /%$/.test(t) ? (parseFloat(t) / 100) * len : parseFloat(t));
  st.crop = (spec) => {
    calls.push(['crop', spec]);
    const d = dims(), r = st.cropRect;
    const x1 = tok(spec.x1, r.x, d.w), x2 = tok(spec.x2, r.x + r.width, d.w), y1 = tok(spec.y1, r.y, d.h), y2 = tok(spec.y2, r.y + r.height, d.h);
    const next = roundRect({ x: Math.min(x1, x2), y: Math.min(y1, y2), width: Math.abs(x2 - x1), height: Math.abs(y2 - y1) }, d.w, d.h);
    const change = cropChangeJS(st.cropRect, next);
    if (change.orientationChanged) st.lines = []; else if (change.scale !== 1) scaleLinePoints(st.lines, change.scale);
    st.cropRect = next;
    return st;
  };
  const turn = (cw) => {
    const d = dims();
    rotateLinePointsQuarter(st.lines, st.cropRect.width, st.cropRect.height, cw);
    st.q = (st.q + (cw ? 1 : 3)) % 4;
    const nd = dims();
    st.cropRect = roundRect(rotateCropRectQuarterJS(st.cropRect, d.w, d.h, cw), nd.w, nd.h);
  };
  st.rotateLeft = () => { calls.push(['rotateLeft']); turn(false); return st; };
  st.rotateRight = () => { calls.push(['rotateRight']); turn(true); return st; };
  st.apply = (o) => {
    calls.push(['apply', o]);
    for (const k of ['filter', 'filterColor', 'pageSize', 'allowFormulas', 'formulaX', 'formulaY', 'showPoints', 'showLines']) if (o[k] != null) st[k] = o[k];
    if (o.page != null) st.pageSize = o.page;
    return st;
  };
  st.setLines = (lines, opts) => { calls.push(['setLines', lines, opts]); st.lines = JSON.parse(JSON.stringify(lines || [])); return st; };
  // load(): the exported pixels become the ORIGINAL (best case: a full-frame crop), lines go.
  st.load = async (url) => {
    calls.push(['load']);
    const shot = JSON.parse(url);
    st.original = { w: shot.w, h: shot.h, data: Uint8Array.from(shot.px) };
    st.q = 0; st.cropRect = { x: 0, y: 0, width: shot.w, height: shot.h }; st.lines = [];
    return st;
  };
  st.blank = async () => { calls.push(['blank']); st.original = { w: 4, h: 4, data: new Uint8Array(64).fill(255) }; st.q = 0; st.cropRect = { x: 0, y: 0, width: 4, height: 4 }; st.lines = []; return st; };
  return st;
};

const snapshot = (st) => JSON.stringify({ q: st.q, crop: st.cropRect, lines: st.lines, filter: st.filter, page: st.pageSize });
const runPlan = (st, p) => executeOpPlan(parseOpPlan(plan(p)), st, { exportImage: st.exportImage });
const LINES = [{ points: [{ x: 1, y: 2 }, { x: 5, y: 3 }], color: '#00ff00' }];

// The pre-rewrite restore, kept as the reference: reload the clean pixel export, then
// put the settings and the lines back.
const referenceRestore = async (st, pixels, state) => {
  await st.load(pixels);
  st.apply({ filter: state.filter, filterColor: state.filterColor, pageSize: state.pageSize,
    allowFormulas: state.allowFormulas, formulaX: state.formulaX, formulaY: state.formulaY });
  if (state.lines.length) st.setLines(state.lines, { history: false });
};

const VARIANTS = {
  crop: [{ op: 'crop', spec: { x1: '25%', y2: '-1px' } }],
  rotR: [{ op: 'rotate', dir: 'right' }],
  rot2: [{ op: 'rotate', dir: 'right', times: 2 }],
  rotL: [{ op: 'rotate', dir: 'left' }],
  rotThenCrop: [{ op: 'rotate', dir: 'left' }, { op: 'crop', spec: { x1: '1px', x2: '5px', y1: '1px', y2: '7px' } }],
  cropThenRot: [{ op: 'crop', spec: { x1: '2px', x2: '7px', y1: '0px', y2: '5px' } }, { op: 'rotate', dir: 'right' }],
  layout: [{ op: 'layout', lines: [{ points: [{ x: 3, y: 3 }] }] }],
  tint: [{ op: 'filter', mode: 'sepia' }],
};

for (const [name, actions] of Object.entries(VARIANTS)) {
  test(`both restores leave byte-identical pixels and state: ${name}`, async () => {
    const before = makeStencil(8, 6, { lines: JSON.parse(JSON.stringify(LINES)), filter: 'bw', pageSize: 'A3' });
    const want = await before.exportImage();
    const wantState = snapshot(before);
    // Reference: clean snapshot → the ops as TOP-LEVEL actions (no sandbox) → reload + write back.
    const ref = makeStencil(8, 6, { lines: JSON.parse(JSON.stringify(LINES)), filter: 'bw', pageSize: 'A3' });
    const state = captureEditorState(ref);
    const pixels = await capturePixels(ref, ref.exportImage);
    await runPlan(ref, { actions });
    await referenceRestore(ref, pixels, state);
    // New: run through the sandbox alone.
    const st = makeStencil(8, 6, { lines: JSON.parse(JSON.stringify(LINES)), filter: 'bw', pageSize: 'A3' });
    const out = await runPlan(st, { variants: [{ label: 'v', actions }] });
    assert.equal(out.results.length, 1);
    assert.equal(await st.exportImage(), want, 'the working image is back, pixel for pixel');
    assert.equal(await ref.exportImage(), want, 'the reference restore agrees');
    assert.equal(snapshot(st), wantState);
    assert.equal(snapshot(ref), wantState);
    assert.equal(st.calls.filter((c) => c[0] === 'load').length, 0, 'no reload, no re-encode');
  });
}

test('a rotated variant is turned back with the fewest quarter turns', async () => {
  const turnsFor = async (dir, times) => {
    const st = makeStencil();
    await runPlan(st, { variants: [{ label: 'v', actions: [{ op: 'rotate', dir, times }] }] });
    return st.calls.filter((c) => /^rotate/.test(c[0])).map((c) => c[0]).slice(times);
  };
  assert.deepEqual(await turnsFor('right', 1), ['rotateLeft']);
  assert.deepEqual(await turnsFor('right', 2), ['rotateLeft', 'rotateLeft']);
  assert.deepEqual(await turnsFor('right', 3), ['rotateRight']);
  assert.deepEqual(await turnsFor('left', 1), ['rotateRight']);
});

test('a crop variant is re-committed to the saved rect with absolute px tokens', async () => {
  const st = makeStencil(8, 6);
  st.crop({ x1: '1px', x2: '7px', y1: '1px', y2: '5px' });   // the user's own crop
  st.calls.length = 0;
  await runPlan(st, { variants: [{ label: 'v', actions: VARIANTS.crop }] });
  assert.deepEqual(st.calls.filter((c) => c[0] === 'crop').at(-1)[1], { x1: '1px', x2: '7px', y1: '1px', y2: '5px' });
  assert.deepEqual(st.cropRect, { x: 1, y: 1, width: 6, height: 4 });
});

test('a variant that throws mid-way is undone only as far as it got', async () => {
  const st = makeStencil(8, 6);
  const realCrop = st.crop;
  st.crop = (spec) => { if (spec.x1 === '25%') throw new Error('boom'); return realCrop(spec); };
  const before = snapshot(st);
  await assert.rejects(runPlan(st, { variants: [{ label: 'v', actions: [{ op: 'rotate', dir: 'right' }, { op: 'crop', spec: { x1: '25%' } }] }] }), /boom/);
  assert.deepEqual(st.calls.map((c) => c[0]), ['rotateRight', 'rotateLeft', 'apply']);
  assert.equal(snapshot(st), before);
});

test('only blank/frame variants take the clean pixel snapshot and reload it', async () => {
  assert.equal(needsPixelSnapshot([{ op: 'crop' }, { op: 'rotate' }, { op: 'layout' }]), false);
  assert.equal(needsPixelSnapshot([{ op: 'blank' }]), true);
  assert.equal(needsPixelSnapshot([{ op: 'frame' }]), true);
  const st = makeStencil(8, 6, { lines: JSON.parse(JSON.stringify(LINES)), filter: 'sepia' });
  const want = await st.exportImage();
  const shots = [];
  const exportImage = async () => { shots.push({ filter: st.filter, showLines: st.showLines }); return st.exportImage(); };
  await executeOpPlan(parseOpPlan(plan({ variants: [{ label: 'v', actions: [{ op: 'blank', color: '#ffffff' }] }] })), st, { exportImage });
  assert.deepEqual(shots[0], { filter: 'none', showLines: false }, 'the snapshot is taken clean');
  assert.equal(st.calls.filter((c) => c[0] === 'load').length, 1);
  assert.equal(await st.exportImage(), want);
});

test('ask previews restore through the same inverse — and a throwing option undoes only what ran', async () => {
  const st = makeStencil(8, 6, { lines: JSON.parse(JSON.stringify(LINES)) });
  const want = await st.exportImage();
  const ask = { question: 'Which?', options: [
    { label: 'turned', actions: [{ op: 'rotate', dir: 'left' }, { op: 'frame', index: 3 }] },   // frame → throws (no video)
    { label: 'cropped', actions: VARIANTS.cropThenRot },
  ] };
  const { previews, warnings } = await renderAskPreviews(ask, st, { exportImage: st.exportImage });
  assert.deepEqual(previews.map((p) => p.label), ['cropped']);
  assert.equal(warnings.length, 1);
  assert.equal(await st.exportImage(), want);
  assert.equal(st.calls.filter((c) => c[0] === 'load').length, 0);
});

test('restoreWorkingImage with nothing run touches only the settings', async () => {
  const st = makeStencil();
  const state = captureEditorState(st);
  await restoreWorkingImage(st, null, state, []);
  assert.deepEqual(st.calls.map((c) => c[0]), ['apply']);
});

import { test } from 'node:test';
import assert from 'node:assert';
import { buildLayoutPayload, serializeSession, LAYOUT_FIELDS, validateLayout, resolveInsertIdx, fillState, defaultBlankSizePx, mergeLines } from '../js/core/layout.js';

// ── mergeLines (concurrent co-edit conflict resolution) ──
test('mergeLines: unions distinct lines from both editors', () => {
  const a = { points: [{ x: 0, y: 0 }], color: '#f00' };  // server-only
  const shared = { points: [{ x: 1, y: 1 }], color: '#0f0' };
  const b = { points: [{ x: 2, y: 2 }], color: '#00f' };  // local-only
  const merged = mergeLines([shared, a], [shared, b]);
  assert.equal(merged.length, 3);                 // shared not duplicated
  assert.deepEqual(merged, [shared, a, b]);        // server order first, then new local
});
test('mergeLines: identical sets dedupe to one copy', () => {
  const l = { points: [{ x: 0, y: 0 }], color: '#f00' };
  assert.deepEqual(mergeLines([l], [l]), [l]);
});
test('mergeLines: dedupes the same line regardless of property key order', () => {
  // A locally-authored line vs its server round-tripped twin (re-serialized key order).
  const local = { points: [{ x: 1, y: 2 }], color: '#f00', thickness: 2, markerSize: 4, style: 'solid', locked: false, fillColor: 'transparent' };
  const server = { color: '#f00', style: 'solid', thickness: 2, markerSize: 4, locked: false, fillColor: 'transparent', points: [{ x: 1, y: 2 }] };
  assert.equal(mergeLines([server], [local]).length, 1);   // must NOT duplicate
});
test('mergeLines: handles empty / non-array inputs', () => {
  const l = { points: [], color: '#f00' };
  assert.deepEqual(mergeLines([], [l]), [l]);
  assert.deepEqual(mergeLines([l], []), [l]);
  assert.deepEqual(mergeLines(null, [l]), [l]);
  assert.deepEqual(mergeLines([l], null), [l]);
  assert.deepEqual(mergeLines(undefined, undefined), []);
});

// ── buildLayoutPayload ──────────────────────────────────────────
test('buildLayoutPayload passes the lines array through by reference', () => {
    const lines = [{ points: [{ x: 1, y: 2 }] }];
    const out = buildLayoutPayload({ imageWidth: 10, imageHeight: 20, lines });
    assert.strictEqual(out.lines, lines); // same reference, no copy
});

test('buildLayoutPayload preserves the dimension numbers', () => {
    const out = buildLayoutPayload({ imageWidth: 800, imageHeight: 600, lines: [] });
    assert.strictEqual(out.imageWidth, 800);
    assert.strictEqual(out.imageHeight, 600);
});

test('buildLayoutPayload serializes byte-identically to the old inline literal', () => {
    const payload = buildLayoutPayload({ imageWidth: 800, imageHeight: 600, lines: [{ points: [{ x: 1, y: 2 }] }] });
    const expected =
`{
  "imageWidth": 800,
  "imageHeight": 600,
  "lines": [
    {
      "points": [
        {
          "x": 1,
          "y": 2
        }
      ]
    }
  ]
}`;
    assert.strictEqual(JSON.stringify(payload, null, 2), expected);
});

test('buildLayoutPayload includes crop/rotation/filter only when provided', () => {
    const full = buildLayoutPayload({
        imageWidth: 10, imageHeight: 20, lines: [],
        imageFilter: 'bw', filterColor: '#7c3aed',
        cropRect: { x: 1, y: 2, width: 3, height: 4 }, rotationQuarters: 3,
    });
    assert.deepStrictEqual(full.cropRect, { x: 1, y: 2, width: 3, height: 4 });
    assert.strictEqual(full.rotationQuarters, 3);
    assert.strictEqual(full.imageFilter, 'bw');
    assert.strictEqual(full.filterColor, '#7c3aed');

    // Absent → omitted entirely (keeps file-export bytes stable).
    const bare = buildLayoutPayload({ imageWidth: 10, imageHeight: 20, lines: [] });
    assert.ok(!('cropRect' in bare));
    assert.ok(!('rotationQuarters' in bare));
    assert.ok(!('imageFilter' in bare));

    // rotationQuarters 0 is a meaningful value → kept (not dropped as falsy).
    const zeroRot = buildLayoutPayload({ imageWidth: 1, imageHeight: 1, lines: [], rotationQuarters: 0 });
    assert.strictEqual(zeroRot.rotationQuarters, 0);
});

test('buildLayoutPayload includes page format + formulas only when provided', () => {
    const full = buildLayoutPayload({
        imageWidth: 10, imageHeight: 20, lines: [],
        pageSize: 'custom', customPageWidth: 15, customPageHeight: 25,
        allowFormulas: true, formulaX: 'x*2', formulaY: 'y+1',
    });
    assert.strictEqual(full.pageSize, 'custom');
    assert.strictEqual(full.customPageWidth, 15);
    assert.strictEqual(full.customPageHeight, 25);
    assert.strictEqual(full.allowFormulas, true);
    assert.strictEqual(full.formulaX, 'x*2');
    assert.strictEqual(full.formulaY, 'y+1');

    // Absent → omitted entirely (file-export/clipboard bytes stay stable).
    const bare = buildLayoutPayload({ imageWidth: 10, imageHeight: 20, lines: [] });
    assert.ok(!('pageSize' in bare));
    assert.ok(!('customPageWidth' in bare));
    assert.ok(!('customPageHeight' in bare));
    assert.ok(!('allowFormulas' in bare));
    assert.ok(!('formulaX' in bare));
    assert.ok(!('formulaY' in bare));

    // allowFormulas:false and empty formulas are meaningful → kept (not dropped as falsy).
    const off = buildLayoutPayload({ imageWidth: 1, imageHeight: 1, lines: [], allowFormulas: false, formulaX: '', formulaY: '' });
    assert.strictEqual(off.allowFormulas, false);
    assert.strictEqual(off.formulaX, '');
    assert.strictEqual(off.formulaY, '');
});

// ── serializeSession (full localStorage session layout) ─────────
// A plain state object identical to what Storage.#buildLayout resolves from live
// app + viewport. Order/values must stay byte-identical to the old inline literal —
// the cross-tab / reopen round-trip reads it back.
const sampleState = () => ({
  imageWidth: 800, imageHeight: 600,
  cropRect: { x: 1, y: 2, width: 3, height: 4 }, rotationQuarters: 0,
  lines: [{ points: [{ x: 1, y: 2 }] }],
  pageSize: 'A3', customPageWidth: 21, customPageHeight: 29.7, unit: 'cm',
  color: '#FFFF00', thickness: 2, markerSize: 4, style: 'solid',
  showPoints: true, showLines: true, imageFilter: 'none', filterColor: '#7c3aed',
  zoom: 1, scrollLeft: 0, scrollTop: 0,
  imageBaseName: 'pic', imageExt: 'png', imageSource: null, imageResource: null,
  tooltipEnabled: true, tooltipShowPage: true, tooltipShowScreen: true, tooltipShowCoords: true,
  allowFormulas: false, formulaX: '', formulaY: '',
  drawMode: 'line', holdDrawDelay: 500,
  selGlowColor: '#ffc800', hoverRingColor: '#7c3aed', focusRingColor: '#7c3aed', defaultFillColor: '#3399ff',
});

test('serializeSession: pure, no DOM/app needed — projects a plain state object', () => {
  const out = serializeSession(sampleState());
  assert.strictEqual(out.imageWidth, 800);
  assert.strictEqual(out.pageSize, 'A3');
  assert.strictEqual(out.defaultFillColor, '#3399ff');
});

test('serializeSession: passes lines through by reference (no copy)', () => {
  const state = sampleState();
  assert.strictEqual(serializeSession(state).lines, state.lines);
});

test('serializeSession: emits every descriptor field in table (byte) order', () => {
  const keys = Object.keys(serializeSession(sampleState()));
  assert.deepStrictEqual(keys, LAYOUT_FIELDS.map(f => f.key));
  // Guard the exact head order that differs from the export subset (crop/rotation before lines).
  assert.deepStrictEqual(keys.slice(0, 5),
    ['imageWidth', 'imageHeight', 'cropRect', 'rotationQuarters', 'lines']);
});

test('serializeSession serializes byte-identically to the old #buildLayout literal', () => {
  const expected =
`{
  "imageWidth": 800,
  "imageHeight": 600,
  "cropRect": {
    "x": 1,
    "y": 2,
    "width": 3,
    "height": 4
  },
  "rotationQuarters": 0,
  "lines": [
    {
      "points": [
        {
          "x": 1,
          "y": 2
        }
      ]
    }
  ],
  "pageSize": "A3",
  "customPageWidth": 21,
  "customPageHeight": 29.7,
  "unit": "cm",
  "color": "#FFFF00",
  "thickness": 2,
  "markerSize": 4,
  "style": "solid",
  "showPoints": true,
  "showLines": true,
  "imageFilter": "none",
  "filterColor": "#7c3aed",
  "zoom": 1,
  "scrollLeft": 0,
  "scrollTop": 0,
  "imageBaseName": "pic",
  "imageExt": "png",
  "imageSource": null,
  "imageResource": null,
  "tooltipEnabled": true,
  "tooltipShowPage": true,
  "tooltipShowScreen": true,
  "tooltipShowCoords": true,
  "allowFormulas": false,
  "formulaX": "",
  "formulaY": "",
  "drawMode": "line",
  "holdDrawDelay": 500,
  "selGlowColor": "#ffc800",
  "hoverRingColor": "#7c3aed",
  "focusRingColor": "#7c3aed",
  "defaultFillColor": "#3399ff"
}`;
  assert.strictEqual(JSON.stringify(serializeSession(sampleState()), null, 2), expected);
});

// ── validateLayout ──────────────────────────────────────────────
test('validateLayout rejects when no image is loaded', () => {
    const r = validateLayout({ lines: [] }, { hasImage: false, imgW: 10, imgH: 10, hasExistingLines: false });
    assert.strictEqual(r.ok, false);
    assert.strictEqual(r.reason, 'no-image');
});

test('validateLayout: image, no existing lines, matching dims → clean apply', () => {
    const lines = [{ points: [{ x: 1, y: 2 }], color: '#f00' }];
    const r = validateLayout({ imageWidth: 100, imageHeight: 50, lines }, { hasImage: true, imgW: 100, imgH: 50, hasExistingLines: false });
    assert.strictEqual(r.ok, true);
    assert.strictEqual(r.needsReplaceConfirm, false);
    assert.strictEqual(r.needsDimMismatchConfirm, false);
    // lines are sanitized (rebuilt on a fresh array), not passed through by reference.
    assert.notStrictEqual(r.lines, lines);
    assert.deepStrictEqual(r.lines, [{ points: [{ x: 1, y: 2 }], color: '#f00', locked: false }]);
});

test('validateLayout flags replace-confirm when lines already exist', () => {
    const r = validateLayout({ imageWidth: 100, imageHeight: 50, lines: [] }, { hasImage: true, imgW: 100, imgH: 50, hasExistingLines: true });
    assert.strictEqual(r.needsReplaceConfirm, true);
});

test('validateLayout flags dim-mismatch when width differs', () => {
    const r = validateLayout({ imageWidth: 99, imageHeight: 50, lines: [] }, { hasImage: true, imgW: 100, imgH: 50, hasExistingLines: false });
    assert.strictEqual(r.needsDimMismatchConfirm, true);
});

test('validateLayout flags dim-mismatch when height differs', () => {
    const r = validateLayout({ imageWidth: 100, imageHeight: 49, lines: [] }, { hasImage: true, imgW: 100, imgH: 50, hasExistingLines: false });
    assert.strictEqual(r.needsDimMismatchConfirm, true);
});

test('validateLayout defaults lines to [] when the field is missing', () => {
    const r = validateLayout({ imageWidth: 100, imageHeight: 50 }, { hasImage: true, imgW: 100, imgH: 50, hasExistingLines: false });
    assert.deepStrictEqual(r.lines, []);
});

test('validateLayout can flag both replace and dim-mismatch at once', () => {
    const r = validateLayout({ imageWidth: 1, imageHeight: 2, lines: [] }, { hasImage: true, imgW: 100, imgH: 50, hasExistingLines: true });
    assert.strictEqual(r.needsReplaceConfirm, true);
    assert.strictEqual(r.needsDimMismatchConfirm, true);
});

// ── validateLayout sanitization (untrusted #stencil= / paste / co-edit ingress) ──
const ctx = { hasImage: true, imgW: 100, imgH: 50, hasExistingLines: false };

test('validateLayout: a __proto__-carrying line never pollutes Object.prototype', () => {
    const evil = JSON.parse('{"imageWidth":100,"imageHeight":50,"lines":[{"points":[],"__proto__":{"polluted":1},"constructor":{"x":1}}]}');
    const r = validateLayout(evil, ctx);
    assert.strictEqual(({}).polluted, undefined);   // no global pollution
    assert.strictEqual(({}).x, undefined);
    // the rebuilt line carries only whitelisted fields (no __proto__/constructor own key)
    assert.deepStrictEqual(Object.keys(r.lines[0]).sort(), ['locked', 'points']);
});

test('validateLayout: non-array lines → [] and non-object data is tolerated', () => {
    assert.deepStrictEqual(validateLayout({ imageWidth: 100, imageHeight: 50, lines: 'nope' }, ctx).lines, []);
    assert.deepStrictEqual(validateLayout({ imageWidth: 100, imageHeight: 50, lines: { 0: 'x' } }, ctx).lines, []);
    assert.deepStrictEqual(validateLayout(null, ctx).lines, []);
});

test('validateLayout: drops non-finite/non-numeric point coords', () => {
    const data = { imageWidth: 100, imageHeight: 50, lines: [{ points: [
        { x: 1, y: 2 }, { x: 'a', y: 3 }, { x: Infinity, y: 0 }, { x: NaN, y: NaN }, { x: 5, y: 6 },
    ] }] };
    assert.deepStrictEqual(validateLayout(data, ctx).lines[0].points, [{ x: 1, y: 2 }, { x: 5, y: 6 }]);
});

test('validateLayout: caps a hostile explosion of lines and points', () => {
    const manyLines = { imageWidth: 100, imageHeight: 50, lines: Array.from({ length: 50010 }, () => ({ points: [] })) };
    assert.strictEqual(validateLayout(manyLines, ctx).lines.length, 50000);

    const manyPoints = { imageWidth: 100, imageHeight: 50, lines: [{ points: Array.from({ length: 100010 }, (_, i) => ({ x: i, y: i })) }] };
    assert.strictEqual(validateLayout(manyPoints, ctx).lines[0].points.length, 100000);
});

// ── resolveInsertIdx ────────────────────────────────────────────
const line4 = { points: [{}, {}, {}, {}] }; // length 4

test('resolveInsertIdx returns focusedPtIdx+1 when the focused point is on the selected line', () => {
    assert.strictEqual(resolveInsertIdx(line4, { coordLineIdx: 2, selectedLineIdx: 2, focusedPtIdx: 2 }), 3);
});

test('resolveInsertIdx appends when coordLineIdx differs from selectedLineIdx', () => {
    assert.strictEqual(resolveInsertIdx(line4, { coordLineIdx: 1, selectedLineIdx: 2, focusedPtIdx: 2 }), 4);
});

test('resolveInsertIdx appends when no point is focused (-1)', () => {
    assert.strictEqual(resolveInsertIdx(line4, { coordLineIdx: 2, selectedLineIdx: 2, focusedPtIdx: -1 }), 4);
});

test('resolveInsertIdx handles focusedPtIdx===0 boundary', () => {
    assert.strictEqual(resolveInsertIdx(line4, { coordLineIdx: 2, selectedLineIdx: 2, focusedPtIdx: 0 }), 1);
});

test('resolveInsertIdx falls back to length 0 for an empty line', () => {
    assert.strictEqual(resolveInsertIdx({ points: [] }, { coordLineIdx: 0, selectedLineIdx: 0, focusedPtIdx: -1 }), 0);
});

// ── fillState ───────────────────────────────────────────────────
test('fillState: undefined fillColor → disabled, value is the default', () => {
    assert.deepStrictEqual(fillState({}, '#112233'), { enabled: false, value: '#112233' });
});

test('fillState: transparent fillColor → disabled, value is the default', () => {
    assert.deepStrictEqual(fillState({ fillColor: 'transparent' }, '#112233'), { enabled: false, value: '#112233' });
});

test('fillState: a real fill color → enabled, value is that color', () => {
    assert.deepStrictEqual(fillState({ fillColor: '#abcdef' }, '#112233'), { enabled: true, value: '#abcdef' });
});

test('fillState: no default supplied falls back to #3399ff', () => {
    assert.deepStrictEqual(fillState({}, undefined), { enabled: false, value: '#3399ff' });
});

// ── defaultBlankSizePx ──────────────────────────────────────────
test('defaultBlankSizePx renders A4/A3 pages at 96 dpi', () => {
    assert.deepStrictEqual(defaultBlankSizePx({ width: 21, height: 29.7 }), { width: 794, height: 1123 });
    assert.deepStrictEqual(defaultBlankSizePx({ width: 29.7, height: 42 }), { width: 1123, height: 1587 });
});

test('defaultBlankSizePx honors a custom dpi', () => {
    assert.deepStrictEqual(defaultBlankSizePx({ width: 2.54, height: 5.08 }, 100), { width: 100, height: 200 });
});

test('defaultBlankSizePx never collapses below 1px', () => {
    assert.deepStrictEqual(defaultBlankSizePx({ width: 0, height: 0.001 }), { width: 1, height: 1 });
});

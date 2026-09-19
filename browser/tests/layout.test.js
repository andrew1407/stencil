import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { buildLayoutPayload, serializeSession, LAYOUT_FIELDS, validateLayout, resolveInsertIdx, fillState, defaultBlankSizePx, mergeLines, sanitizeLines } from '../js/core/layout.js';

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
  const local = { points: [{ x: 1, y: 2 }], color: '#f00', thickness: 2, pointSize: 4, style: 'solid', locked: false, fillColor: 'transparent' };
  const server = { color: '#f00', style: 'solid', thickness: 2, pointSize: 4, locked: false, fillColor: 'transparent', points: [{ x: 1, y: 2 }] };
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
    assert.strictEqual(out.lines, lines);
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
    // The internal {width,height} rect leaves in the canonical {w,h} wire spelling.
    assert.deepStrictEqual(full.cropRect, { x: 1, y: 2, w: 3, h: 4 });
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

test('buildLayoutPayload emits cropRect canonically ({w,h} only, both input spellings)', () => {
    // Live path: currentLayoutPayload passes the app's internal {width,height} rect.
    const legacy = buildLayoutPayload({ imageWidth: 10, imageHeight: 20, lines: [], cropRect: { x: 1, y: 2, width: 3, height: 4 } });
    assert.deepStrictEqual(legacy.cropRect, { x: 1, y: 2, w: 3, h: 4 });
    assert.ok(!('width' in legacy.cropRect) && !('height' in legacy.cropRect));
    // Canonical input passes through unchanged (what the corpus fixtures pin).
    const canon = buildLayoutPayload({ imageWidth: 10, imageHeight: 20, lines: [], cropRect: { x: 1, y: 2, w: 3, h: 4 } });
    assert.deepStrictEqual(canon.cropRect, { x: 1, y: 2, w: 3, h: 4 });
    // Both spellings present → canonical wins.
    const both = buildLayoutPayload({ imageWidth: 1, imageHeight: 1, lines: [], cropRect: { x: 0, y: 0, w: 5, h: 6, width: 9, height: 9 } });
    assert.deepStrictEqual(both.cropRect, { x: 0, y: 0, w: 5, h: 6 });
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

// A plain state object identical to what Storage.#buildLayout resolves from the live app and viewport;
// order and values stay byte-identical, since the cross-tab / reopen round-trip reads it back.

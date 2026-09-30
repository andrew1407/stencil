// The Line/Rect glyph pair: siblings on the 16-grid, the canonical 24-grid pair scaled, and the
// rect tool turning drawing on for its own sweep. Split from drawToggle.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createStubElement, installDom } from '../helpers/dom.js';

// Line and Rect are one toggle, so the faces are a matched SET: same box, same stroke, the same two
// handles — (3,13) and (13,3), the corners a drag starts and ends on — only the shape differs.
test('the Line and Rect glyphs are siblings, not two different families', async () => {
  const { DRAW_MODE_ICON } = await import('../../js/ui/icons.js');
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

// The pair lives in the CANONICAL set too (config/icons.json `line`/`rect`), where the desktop and
// the extension read it: the toolbar's inline 16-grid pair is that 24-grid drawing scaled x1.5.
test('the canonical line/rect pair is the inline pair, scaled onto the 24-grid', async () => {
  const { DRAW_MODE_ICON } = await import('../../js/ui/icons.js');
  const ICONS = JSON.parse(readFileSync(new URL('../../../common/config/icons.json', import.meta.url), 'utf8'));
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

// Picking the rect tool is the intent, so the press turns drawing on itself: the sweep required
// drawing mode to be on already, and rect has no hold-to-draw flow to fall back on.
// One plain left press on the canvas in rect mode, through the real startDrawingMode; `grants` is
// whether it turns drawing on. A declined start is read-only by the time it asks (the gate passed).
const pressRect = async ({ drawing, grants }) => {
  const doc = installDom({}, { window: { innerWidth: 800, innerHeight: 600 } });
  doc.register('canvas-viewport', createStubElement('div'));
  try {
    const { PointerController } = await import('../../js/core/pointer/controller.js');
    const { Emitter } = await import('../../js/core/emitter.js');
    const { CHANGE } = await import('../../js/core/app/changes.js');
    const log = [];
    let asked = 0;
    // Drawn at half size: a client px is two image px.
    const canvas = createStubElement('canvas', {
      width: 200, height: 200, getBoundingClientRect: () => ({ left: 0, top: 0, width: 100, height: 100 }),
    });
    const app = {
      drawMode: 'rect', image: {}, isDrawing: drawing, canvas, lines: [], selectedLineIdx: -1,
      color: '#ff0000', thickness: 2, pointSize: 4, style: 'solid',
      compareReadOnly: () => !grants && ++asked > 1,
      hideSelectionPanels() {}, renderer: { redraw() {} }, changes: new Emitter(),
    };
    app.changes.on(CHANGE.drawing, () => log.push(`start:${!!app.isRectDrawDragging}`));
    new PointerController(app).wirePanDrag();
    const e = { button: 0, clientX: 10, clientY: 20, preventDefault: () => log.push('prevent'), stopPropagation() {} };
    app.canvas.dispatch('mousedown', e);
    return { log, sweeping: !!app.isRectDrawDragging, from: app.rectDrawStart, asked };
  } finally {
    doc.restore();
  }
};

test('the rect tool starts its sweep on the press, turning drawing on by itself', async () => {
    // The gate does not demand isDrawing: the press turns it on before the band starts.
    const off = await pressRect({ drawing: false, grants: true });
    assert.deepEqual(off.log, ['start:false', 'prevent'], 'drawing goes on before the band starts');
    assert.equal(off.sweeping, true, 'the sweep must not require drawing mode to be on already');
    assert.deepEqual(off.from, { imgX: 20, imgY: 40, cssX: 10, cssY: 20 });
    // Already drawing: nothing to turn on.
    const on = await pressRect({ drawing: true, grants: true });
    assert.deepEqual([on.log, on.sweeping], [['prevent'], true]);
    // Declined (no image / read-only): it gives up cleanly, with nothing swept or swallowed.
    const declined = await pressRect({ drawing: false, grants: false });
    assert.deepEqual([declined.log, declined.sweeping], [[], false]);
    assert.equal(declined.asked, 2, 'the press tried the start, which declined');
    // The desktop press does the same, so the two tools behave alike.
    const cpp = readFileSync(new URL('../../../desktop/src/canvas/draw/CanvasDrawClick.cpp', import.meta.url), 'utf8');
    const dbranch = cpp.slice(cpp.indexOf('// rect-draw press'), cpp.indexOf('// when not drawing, a left-click'));
    assert.match(dbranch, /if \(drawMode == DrawMode::RECT && mods == Qt::NoModifier\)/);
    assert.match(dbranch, /if \(!isDrawing\) startDrawingMode\(\);/);
});

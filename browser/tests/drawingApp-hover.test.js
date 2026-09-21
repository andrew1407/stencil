// DrawingApp.updateInfo and canvasMouseMove (js/core/drawingApp.js): the info line's incognito
// tag, the coords readout in every compare state, and the pointer-transparent overlays.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { installDom } from './helpers/dom.js';
import { COMPONENTS_CSS } from './helpers/css.js';

installDom({}, { location: { hash: '', pathname: '/app', search: '' }, history: { replaceState: () => {} } });

const { DrawingApp } = await import('../js/core/drawingApp.js');

// The info line carries its own incognito tag, since the "?" bubble appears only with an image and
// an empty incognito editor announced it nowhere (user report); data-size stays the size alone.
const infoLine = () => {
  const el = { dataset: {}, children: [], _text: '', className: '', innerHTML: '' };
  el.setAttribute = () => {};
  el.appendChild = (c) => { el.children.push(c); return c; };
  el.append = (...cs) => { for (const c of cs) el.appendChild(c); };
  Object.defineProperty(el, 'textContent', {
    get: () => el._text + el.children.map((c) => c.textContent).join(''),
    set: (v) => { el._text = String(v); el.children.length = 0; },
  });
  return el;
};
const runUpdateInfo = (over) => {
  const info = infoLine();
  const prev = globalThis.document;
  globalThis.document = {
    getElementById: (id) => (id === 'image-info' ? info : null),
    createElement: () => infoLine(),
  };
  try {
    DrawingApp.prototype.updateInfo.call({
      image: null, canvas: { width: 0, height: 0 },
      activeIsBlank: () => false, storage: { incognito: false }, ...over,
    });
  } finally { globalThis.document = prev; }
  return info;
};

test('the info line states incognito with no image loaded, and nothing when it is off', () => {
  const on = runUpdateInfo({ storage: { incognito: true } });
  assert.equal(on.dataset.size, 'No image loaded. Upload an image to start.');
  // A muted divider then the tag — the empty editor gets the same pair as a loaded one.
  assert.equal(on.children.length, 2, 'the empty editor still shows the mode');
  assert.equal(on.children[0].className, 'info-divider');
  assert.equal(on.children[0].textContent, '|');
  assert.equal(on.children[1].className, 'info-incognito');
  // The app's own glyph, at 13px, stroked in currentColor so it takes the accent.
  assert.match(on.children[1].innerHTML, /class="ic ic-incognito"/);
  assert.match(on.children[1].innerHTML, /width="13" height="13"/);
  assert.match(on.children[1].innerHTML, /stroke="currentColor"/);
  assert.ok(!/🕶/.test(on.children[1].innerHTML), 'no emoji — it renders differently per platform');
  assert.match(on.children[1].innerHTML, /Incognito — not saved/);
  // …and the size stays separable, so the "?" bubble never doubles the line.
  assert.ok(!on.dataset.size.includes('Incognito'));

  const off = runUpdateInfo({});
  assert.equal(off.children.length, 0, 'no tag — and so no dangling divider — when it is off');
  assert.equal(off.textContent, 'No image loaded. Upload an image to start.');

  // With an image the tag rides beside the size, which data-size still holds alone.
  const withImage = runUpdateInfo({
    image: {}, canvas: { width: 800, height: 600 }, storage: { incognito: true },
  });
  assert.equal(withImage.dataset.size, 'Image Size: 800 × 600 px');
  assert.equal(withImage.children.length, 2, 'divider + tag beside the image facts');
});

// Nothing overlays the canvas — the incognito frame is pointer-transparent and a split is drawn
// INTO it — so canvasMouseMove must update the readout on the compare branches too.
const hoverMock = (over = {}) => {
  const readout = [];
  return {
    readout,
    image: {}, isPanning: false, isDraggingPoint: false,
    compareHoldOriginal: false, isDraggingCompareSplit: false, compareMode: 'horizontal',
    canvas: { style: {} },
    input: { holdEngaged: false },
    tooltipMgr: { hide() {}, applyHover() {} },
    // The compare gate the tooltip consults (drawingApp.compareShowsPoint).
    compareShowsPoint: () => true,
    isDrawing: false, drawMode: 'line', isZoomRectDragging: false,
    hoverPt: null, hoverLineIdx: -1, coordLineIdx: 0, hoveredPtIdx: -1,
    findNearestSegmentWithIdx: () => null,
    canvasCoords: () => ({ x: 12, y: 34 }),
    updateCoordStatus: (...a) => readout.push(a),
    nearCompareDivider: () => false,
    compareReadOnly: () => true,
    findNearestPointWithIdx: () => null,
    findLineAt: () => -1,
    coordTable: { applyRowHighlight() {} },
    renderer: { redraw() {} },
    applyLinesListHover() {},
    ...over,
  };
};

test('hovering the canvas updates the coords readout in ALL four states', () => {
  const ev = (x) => ({ clientX: x, clientY: 120, altKey: false, shiftKey: false, ctrlKey: false, metaKey: false });
  // Desktop parity (CanvasWidget::mouseMoveEvent): the readout must CHANGE between two cursor
  // positions in every state — plain, incognito, and both split compares.
  const states = [
    ['plain', { compareMode: 'none', compareReadOnly: () => false }],
    ['incognito', { compareMode: 'none', compareReadOnly: () => false, incognito: true }],
    ['compare vertical', { compareMode: 'vertical', compareReadOnly: () => true }],
    ['compare horizontal', { compareMode: 'horizontal', compareReadOnly: () => true }],
  ];
  for (const [name, over] of states) {
    let n = 0;
    const m = hoverMock({ ...over, canvasCoords: () => ({ x: 10 + (n += 1) * 5, y: 34 }) });
    DrawingApp.prototype.canvasMouseMove.call(m, ev(100));
    DrawingApp.prototype.canvasMouseMove.call(m, ev(140));
    assert.deepEqual(m.readout, [[15, 34], [20, 34]], `${name}: the readout follows the cursor`);
  }
  // Over the draggable divider it still reports — and the handle keeps its resize cursor.
  const div = hoverMock({ nearCompareDivider: () => true, compareReadOnly: () => false });
  DrawingApp.prototype.canvasMouseMove.call(div, ev(100));
  assert.deepEqual(div.readout, [[12, 34]]);
  assert.equal(div.canvas.style.cursor, 'row-resize', 'the handle stays draggable');
  // Compare is still read-only for EDITING — but the coordinate tooltip is DISPLAY, so
  // the hover decision now runs (its own visibility gate decides what to show).
  const cmp = hoverMock();
  const asked = [];
  cmp.tooltipMgr = { hide() {}, applyHover: (...a) => asked.push(a.slice(0, 4)) };
  DrawingApp.prototype.canvasMouseMove.call(cmp, ev(100));
  assert.deepEqual(asked, [[100, 120, 12, 34]], 'the tooltip is offered the hovered point');
  assert.equal(cmp.canvas.style.cursor, 'default', 'while the edit cursor stays suppressed');
  // Over the DIVIDER nothing is offered — dragging it must not pop a point tooltip.
  const onDiv = hoverMock({ nearCompareDivider: () => true, compareReadOnly: () => false });
  let divHidden = 0;
  onDiv.tooltipMgr = { hide: () => { divHidden += 1; },
    applyHover: () => assert.fail('the divider must not pop a tooltip') };
  DrawingApp.prototype.canvasMouseMove.call(onDiv, ev(100));
  assert.equal(divHidden, 1);
  // No image at all → the idle hint, as before (no coordinates to report).
  const empty = hoverMock({ image: null });
  DrawingApp.prototype.canvasMouseMove.call(empty, ev(100));
  assert.deepEqual(empty.readout, [[]]);
});

// Panning, dragging, sweeping a zoom/rect-draw box or an armed press-and-hold (InputController
// #holdDraw) all move the mouse with no tooltip, and drop any tooltip already up.
test('a drag or hold in progress (any kind) never offers the tooltip a hover — and drops one already up', () => {
  const ev = (x) => ({ clientX: x, clientY: 120, altKey: false, shiftKey: false, ctrlKey: false, metaKey: false });
  const cases = [
    ['isPanning', { isPanning: true }],
    ['isDraggingPoint', { isDraggingPoint: true }],
    ['isDraggingSegment', { isDraggingSegment: true }],
    ['isDraggingLine', { isDraggingLine: true }],
    ['isZoomRectDragging', { isZoomRectDragging: true }],
    ['isRectDrawDragging', { isRectDrawDragging: true }],
    ['input.holdEngaged (armed or drawing)', { input: { holdEngaged: true } }],
  ];
  for (const [name, over] of cases) {
    const m = hoverMock({ compareMode: 'none', compareReadOnly: () => false, ...over });
    let hidden = 0;
    m.tooltipMgr = { hide: () => { hidden += 1; },
      applyHover: () => assert.fail(`${name}: a drag/hold in progress must not pop a tooltip`) };
    DrawingApp.prototype.canvasMouseMove.call(m, ev(100));
    assert.equal(hidden, 1, `${name}: any tooltip already up is dropped`);
    assert.deepEqual(m.readout, [], `${name}: the coord readout is not touched mid-gesture either`);
  }
});

test('the on-canvas overlays never eat the pointer', () => {
  const css = COMPONENTS_CSS;
  const frame = css.slice(css.indexOf('.incognito-frame {'), css.indexOf('}', css.indexOf('.incognito-frame {')));
  assert.match(frame, /pointer-events: none/, 'the incognito frame is pointer-transparent');
  // pointer-events inherits, so the four dashed edges inside it must not re-enable it.
  const edge = css.slice(css.indexOf('.ig-edge {'), css.indexOf('}', css.indexOf('.ig-edge {')));
  assert.ok(!/pointer-events/.test(edge), 'the edges inherit the frame\'s transparency');
  // The comparison has no overlay element to eat anything — the split is rendered
  // into the canvas itself, and its divider is a cursor + a pointer handler.
  const markup = readFileSync(new URL('../js/ui/panel/mainContent.js', import.meta.url), 'utf8');
  assert.ok(!/compare-(overlay|divider|handle)/.test(markup), 'the split stays a canvas render');
  const zoomRect = markup.slice(markup.indexOf('id="zoom-rect-overlay"'));
  assert.match(zoomRect.slice(0, zoomRect.indexOf('>')), /pointer-events:none/, 'and the zoom rect too');
});

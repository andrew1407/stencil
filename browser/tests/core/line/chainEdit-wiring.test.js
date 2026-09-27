// The ways back out of a closed shape, wired: the Alt+Ctrl/⌘ pull-out (pointer/controller.js →
// app/editing.js beginPullOutDrag) opens no context menu and wins over the plain Alt gestures, and
// the selection bar's Unchain is offered on areas only. Desktop twin: canvas/input/CanvasDrag.cpp.
import test from 'node:test';
import assert from 'node:assert';
import { installDom, createStubElement } from '../../helpers/dom.js';

const P = (...xy) => xy.map(([x, y]) => ({ x, y }));
const area = () => ({ locked: true, fillColor: 'transparent', points: P([0, 0], [10, 0], [10, 10], [0, 0]) });

test('the pull-out chord opens no context menu', async () => {
  // On macOS Ctrl+click IS the secondary click, so the Alt+Ctrl drag has to suppress `contextmenu`
  // while a plain Ctrl+click still gets its menu (user report).
  const { wireContextMenu } = await import('../../helpers/ctxMenuChatRig.js');
  const rig = await wireContextMenu();
  const press = (mods) => {
    let prevented = 0;
    rig.doc.getElementById('canvas').fire('contextmenu',
      { clientX: 200, clientY: 150, ctrlKey: true, ...mods, preventDefault: () => { prevented++; } });
    return prevented;
  };
  assert.strictEqual(press({ altKey: true }), 1, 'the native menu is suppressed either way');
  assert.strictEqual(rig.isOpen(), false, 'Alt means the gesture, not a menu — ours never opens for it');
  assert.strictEqual(press({}), 1);
  assert.strictEqual(rig.isOpen(), true, 'a plain Ctrl+click still gets its menu');
});

// The press handlers wired on a stub canvas; `press(mods)` fires one left mousedown.
const wirePointer = async (app) => {
  const { PointerController } = await import('../../../js/core/pointer/controller.js');
  const doc = installDom({}, { window: { innerWidth: 800, innerHeight: 600 } });
  doc.register('canvas-viewport', createStubElement('div'));
  // Client px are image px: the canvas sits at the origin at 1:1 (pointer/canvasCoords.js).
  app.canvas = createStubElement('canvas', { width: 100, height: 100,
    getBoundingClientRect: () => ({ left: 0, top: 0, width: 100, height: 100 }) });
  new PointerController(app).wirePanDrag();
  return (mods) => app.canvas.dispatch('mousedown',
    { button: 0, clientX: 10, clientY: 0, preventDefault() {}, stopPropagation() {}, ...mods });
};

test('Alt+Ctrl/⌘+drag is checked before the plain Alt gestures', async () => {
  // Left to the plain Alt branch, the press would MOVE the point already there instead
  // of pulling a new one out of it.
  const pulls = [];
  const nearPt = { lineIdx: 0, ptIdx: 1 };
  const app = {
    image: {}, lines: [{ points: P([0, 0], [10, 0]) }], compareReadOnly: () => false, compareMode: 'none',
    beginPullOutDrag: (x, y) => { pulls.push([x, y]); return true; },
    findNearestPointWithIdx: () => nearPt, findLineAt: () => 0, selectedLines: [], selectedLineIdx: -1,
  };
  const press = await wirePointer(app);
  for (const mods of [{ altKey: true, ctrlKey: true }, { altKey: true, metaKey: true }]) {
    press(mods);
    assert.notStrictEqual(app.draggingPoint, nearPt, 'the pull-out must be tested first');
  }
  assert.deepStrictEqual(pulls, [[10, 0], [10, 0]], 'Ctrl and ⌘ both pull');
  press({ altKey: true, ctrlKey: true, shiftKey: true });
  assert.strictEqual(pulls.length, 2, 'with Shift it is the whole-line drag instead');
  press({ altKey: true });
  assert.deepStrictEqual([pulls.length, app.draggingPoint], [2, nearPt], 'plain Alt still moves the point');
});

test('the pull-out selects the line it broke and drags the new point', async () => {
  const { EditingMethods } = await import('../../../js/core/app/editing.js');
  installDom();
  const make = (mode) => Object.assign(Object.create(EditingMethods.prototype), {
    lines: [area()], selectedLineIdx: -1, scale: 1, strokeFx: { flyIn() {} },
    renderer: { effectiveCompareMode: () => mode, redraw() {} }, coordTable: { update() {} },
    showSelectionPanel() {}, findNearestPointWithIdx: () => ({ lineIdx: 0, ptIdx: 1 }),
  });
  const app = make('none');
  assert.strictEqual(app.beginPullOutDrag(10, 0), true);
  assert.strictEqual(app.selectedLineIdx, 0, 'the broken line is selected');
  assert.strictEqual(app.isDraggingPoint, true, 'and the new point is being dragged');
  assert.deepStrictEqual(app.draggingPoint, { lineIdx: 0, ptIdx: app.focusedPtIdx });
  assert.strictEqual(app.lines[0].points[app.draggingPoint.ptIdx].x, 10, 'the drag holds the pulled point');
  const peek = make('split');
  assert.strictEqual(peek.beginPullOutDrag(10, 0), false, 'never in a read-only compare view');
  assert.deepStrictEqual([peek.selectedLineIdx, peek.isDraggingPoint, peek.lines[0].locked], [-1, undefined, true]);
});

test('Unchain is offered exactly where a line is an area', async () => {
  // #sel-fill-group is already shown only for locked lines (showSelectionPanel), so the
  // button inside it needs no gate of its own.
  const doc = installDom({ autoCreateById: true }, { location: { hash: '', pathname: '/app', search: '' },
    history: { replaceState() {} } });
  const { StencilSelectionPanel } = await import('../../../js/ui/panel/selectionPanel.js');
  const markup = StencilSelectionPanel.inner();
  const group = markup.slice(markup.indexOf('id="sel-fill-group"'), markup.indexOf('id="sel-deselect"'));
  assert.ok(group.includes('id="sel-unchain"'), 'the button lives in the area-only group');
  const { wireSelectionPanelControls } = await import('../../../js/ui/bindings/selectionPanel.js');
  let unchained = 0;
  wireSelectionPanelControls({ unchainSelectedLine: () => { unchained++; } });
  doc.getElementById('sel-unchain').dispatch('click');
  assert.strictEqual(unchained, 1, 'the button unchains the selected line');
  const { DrawingApp } = await import('../../../js/core/drawingApp.js');
  const app = Object.assign(Object.create(DrawingApp.prototype), {
    lines: [area()], selectedLineIdx: 0, renderer: { effectiveCompareMode: () => 'none', redraw() {} },
    coordTable: { update() {} }, showSelectionPanel() {}, saveHistory() {}, updateButtons() {},
  });
  app.unchainSelectedLine();
  assert.strictEqual(app.lines[0].locked, false, 'and the app carries the method it calls');
});

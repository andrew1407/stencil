// A real DrawingApp prototype with its real removals, CoordTable, Lines tab and control areas over a
// stub DOM, for the suites that pin the line panels (core/drawingApp-removal, drawingApp-lineEdits):
// `assertPanels` holds the Lines tab, the points table and the bar to the editor after an edit.
import assert from 'node:assert/strict';
import { installDom, createStubElement } from './dom.js';

export const doc = installDom({ autoCreateById: true }, {
  location: { hash: '', pathname: '/', search: '' }, history: { replaceState: () => {} },
  matchMedia: () => ({ matches: true }),   // reduced motion: a row's leave runs its removal at once
});
doc.querySelectorAll = () => [];
export const linesBody = createStubElement('tbody', {
  replaceChildren() { linesBody.children.length = 0; },
  querySelectorAll: () => linesBody.children.filter((r) => r.classList.contains('lines-row')),
});
export const linesTable = doc.register('lines-list', createStubElement('table', { tBodies: [linesBody] }));

const { DrawingApp } = await import('../../js/core/drawingApp.js');
const { createEditorState } = await import('../../js/core/editorState.js');
const { HistoryStack } = await import('../../js/core/historyStack.js');
const { Emitter } = await import('../../js/core/emitter.js');
const { CoordTable } = await import('../../js/ui/panel/coordTable.js');
const { renderLinesList } = await import('../../js/ui/panel/lines/list.js');
const { updateButtons, wireControlState } = await import('../../js/ui/control/state.js');
const { selectedIndices } = await import('../../js/core/line/selection.js');

export const line = (y, n = 3) => ({ points: Array.from({ length: n }, (_, i) => ({ x: 20 + 40 * i, y })),
  color: '#ff0000', thickness: 2, pointSize: 4, style: 'solid' });

// The points table's body: rows CoordTable appends, cleared by its innerHTML writes.
const pointsBody = () => {
  const body = createStubElement('tbody', {
    rows: [], appendChild(r) { body.rows.push(r); }, querySelectorAll: () => body.rows,
    querySelector: (sel) => body.rows[Number(/"(\d+)"/.exec(sel)?.[1])] ?? null,
  });
  Object.defineProperty(body, 'innerHTML', { set() { body.rows = []; }, get: () => '' });
  return body;
};

export const makeApp = (lines) => {
  const app = Object.create(DrawingApp.prototype);
  Object.assign(app, createEditorState(), {
    image: { width: 400, height: 300 }, originalImage: { width: 400, height: 300 },
    canvas: createStubElement('canvas', {
      width: 400, height: 300, getBoundingClientRect: () => ({ left: 0, top: 0, width: 400, height: 300 }),
    }),
    lines, activeProjectId: 'p1', remoteLink: null, compareMode: 'none', cropRect: { x: 0, y: 0, w: 400, h: 300 },
    openInConfig: { desktopScheme: '', telegramBotUsername: '' },
    changes: new Emitter(), history: new HistoryStack(), coordinatesBody: pointsBody(),
    renderer: { redraw() {}, requestRedraw() {}, effectiveCompareMode: () => app.compareMode },
    storage: { incognito: false, temporary: false, saveSoon() {}, save() {}, store: { getMeta: () => ({ name: 'Plan' }) } },
    remoteSync: { scheduleRemoteSync() {} }, strokeFx: { cancel() {}, flyIn() {} },
    imageModel: { restoreView: () => false, roundRect: (r) => r, settleView() {} },
    stencilSync: { supported: false, linked: false, liveSync: false, name: '' }, tabs: { reportIncognito() {} },
    zoomPan: { syncCoordPanelHeight() {} }, confirm: async () => true,
    // The bar: what it was last shown with, null once hidden.
    bar: null,
    showSelectionPanel(l) { app.bar = l; renderLinesList(app); },
    hideSelectionPanels() { app.bar = null; },
  });
  app.coordTable = new CoordTable(app);
  wireControlState(app);
  app.saveHistory();
  updateButtons(app);
  return app;
};

export const listRows = () => linesBody.children.filter((r) => r.classList.contains('lines-row'));
const countOf = (row) => row.children.find((c) => c.classList.contains('lines-count-cell')).textContent;

// The three panels agree with the editor: the list row for row, the table with its own line, the bar.
export const assertPanels = (app, what) => {
  const rows = listRows();
  assert.equal(rows.length, app.lines.length, `${what}: one Lines row per line`);
  rows.forEach((r, i) => assert.equal(countOf(r), String(app.lines[i].points.length), `${what}: row ${i + 1}'s count`));
  assert.ok(app.selectedLines.every((i) => i >= 0 && i < app.lines.length), `${what}: no multi-selection past the end`);
  const marked = rows.flatMap((r, i) => (r.classList.contains('lines-row-selected') ? [i] : []));
  assert.deepEqual(marked, selectedIndices(app), `${what}: the list marks the selection`);
  assert.equal(app.bar, app.selectedLineIdx >= 0 ? app.lines[app.selectedLineIdx] : null, `${what}: the bar shows the selected line`);
  const target = app.coordLineIdx === -1 ? app.currentLine : app.lines[app.coordLineIdx];
  assert.equal(app.coordinatesBody.rows.length, target ? target.points.length : 0, `${what}: the points table lists its own line`);
  if (selectedIndices(app).length < 2) assert.equal(doc.getElementById('coord-status').textContent, '', `${what}: no stale multi-select note`);
};

// Delete on the points table's first row: it must take the first point of the line the table shows.
export const deleteFirstTablePoint = (app) => {
  const row = app.coordinatesBody.rows[0];
  const target = { tagName: 'TR', closest: (sel) => (sel === 'tr[data-pt-idx]' ? row : null) };
  app.coordinatesBody.dispatch('keydown', { key: 'Delete', target, preventDefault() {}, stopPropagation() {} });
};

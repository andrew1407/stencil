// The "Lines" tab: one row per committed line — its number, its name, its own line colour and
// thickness, its point colour and size, its point count, its eye and its bin — on the points table's
// grid. The app keeps the state; this paints the rows. Desktop twin: app/selection/SelectionPanelLines.cpp.
import { icon } from '../../icons.js';
import { fillState } from '../../../core/layout.js';
import { pointColorOf } from '../../../core/line/render.js';
import { selectionPredicate } from '../../../core/line/selection.js';
import { wireLinesList, takeEyeToggled } from './events.js';
import { paintNameCell } from './nameEdit.js';
import { RowWindow } from '../rowWindow.js';
import constants from '../../../../../common/config/constants.json' with { type: 'json' };

const STROKE = constants.DEFAULT_VISUALS.color;
const { thickMin, thickMax, pointMin, pointMax } = constants.LIMITS;
const COLUMNS = 9;   // #, name, line colour, thickness, point colour, point size, points, eye, bin

const TIPS = Object.freeze({
  color: "Line color\nDouble-click: the toolbar's line color",
  area: "Line color — an area shows its fill inside\nDouble-click: the toolbar's line color",
  thickness: `Line thickness\nDouble-click to edit (${thickMin}–${thickMax} px)`,
  pointColor: "Point color\nDouble-click: the toolbar's point color",
  pointSize: `Point size\nDouble-click to edit (${pointMin}–${pointMax} px)`,
  name: 'Line name\nDouble-click to rename',
});

// Class toggle only — the list is never scrolled by a canvas hover; only the rows it moves off and onto.
export const applyLinesListHover = (app) => {
  const el = document.getElementById('lines-list');
  if (!el) return;
  for (const r of el.querySelectorAll('.lines-row-hover')) if (parseInt(r.dataset.idx, 10) !== app.hoverLineIdx) r.classList.remove('lines-row-hover');
  if (app.hoverLineIdx >= 0) el.querySelector(`.lines-row[data-idx="${app.hoverLineIdx}"]`)?.classList.add('lines-row-hover');
};

const cell = (cls, title) => {
  const td = document.createElement('td');
  if (cls) td.className = cls;
  if (title) td.dataset.title = title;
  return td;
};

const swatchCell = (cls, title, face, rim) => {
  const td = cell('lines-swatch-cell', title);
  const swatch = document.createElement('span');
  swatch.className = cls;
  swatch.style.background = face;
  if (rim) swatch.style.borderColor = rim;
  td.appendChild(swatch);
  return td;
};

// A number the row edits in place on a double-click (./numEdit.js).
const numCell = (prop, value) => {
  const td = cell('lines-num', TIPS[prop]);
  td.dataset.prop = prop;
  td.textContent = String(value);
  return td;
};

const removeCell = (n) => {
  const td = cell('lines-remove-cell');
  const rm = document.createElement('button');
  // The points table's own delete face: one bin, one look, both tabs.
  rm.className = 'lines-remove del-pt-btn btn-icon';
  rm.type = 'button';
  rm.dataset.title = 'Remove line';
  rm.setAttribute('aria-label', `Remove line ${n}`);
  rm.innerHTML = icon('trash', { size: 13 });
  td.appendChild(rm);
  return td;
};

// The eye says what a click will do; the row it just flipped plays its toggle once (lines.css).
const eyeCell = (line, n, flipped) => {
  const td = cell('lines-eye-cell');
  const eye = document.createElement('button');
  eye.className = 'lines-eye btn-icon' + (line.hidden ? ' lines-eye-off' : '') + (flipped ? ' lines-eye-flip' : '');
  eye.type = 'button';
  eye.dataset.title = line.hidden ? 'Show line' : 'Hide line';
  eye.setAttribute('aria-label', `${line.hidden ? 'Show' : 'Hide'} line ${n}`);
  eye.setAttribute('aria-pressed', line.hidden ? 'true' : 'false');
  eye.innerHTML = icon('eye', { size: 13 });
  td.appendChild(eye);
  return td;
};

const nameCell = (line, i) => {
  const td = cell('lines-name', TIPS.name);
  paintNameCell(td, line, i);
  return td;
};

// Everything rowOf draws a row from: a re-render keeps a row whose inputs did not move. A row with an
// editor open in it, or mid-exit (a class rowOf never sets), is built again, as every row once was.
const rowSig = (app, line, i, selected) => {
  const fill = fillState(line, app.defaultFillColor);
  return JSON.stringify([i, selected, i === app.hoverLineIdx, !!line.hidden, !!line.locked, line.name ?? '', line.color ?? '',
    line.thickness, pointColorOf(line), line.pointSize ?? app.pointSize, line.points.length, fill.enabled, fill.value]);
};
const keepable = (row, sig) => row?.dataset?.sig === sig && !row.querySelector?.('input')
  && String(row.className).split(/\s+/).every((c) => !c || c.startsWith('lines-row'));

const rowOf = (app, line, i, selected, flipped) => {
  const row = document.createElement('tr');
  row.className = 'lines-row' + (selected ? ' lines-row-selected' : '') + (line.hidden ? ' lines-row-hidden' : '');
  if (i === app.hoverLineIdx) row.classList.add('lines-row-hover');
  row.dataset.idx = String(i);
  row.tabIndex = 0;
  const index = cell('', `Line ${i + 1}`);
  index.textContent = String(i + 1);
  // What the line LOOKS like: its fill when it has one, its stroke otherwise — the stroke always the
  // rim, so a filled area still says which colour drew it, and an unfilled area is a hollow ring.
  const fill = fillState(line, app.defaultFillColor);
  const stroke = line.color || STROKE;
  const face = fill.enabled ? fill.value : (line.locked ? 'transparent' : stroke);
  const count = cell('lines-count-cell', 'Points');
  count.textContent = String(line.points.length);
  row.append(index, nameCell(line, i), swatchCell('lines-swatch', line.locked ? TIPS.area : TIPS.color, face, stroke),
    numCell('thickness', line.thickness),
    swatchCell('lines-point-swatch', TIPS.pointColor, pointColorOf(line)),
    numCell('pointSize', line.pointSize ?? app.pointSize), count,
    eyeCell(line, i + 1, flipped), removeCell(i + 1));
  return row;
};

// A long list's window (rowWindow.js), one per body: its rows are built as they scroll in, from live state.
const windows = new WeakMap();
const windowOf = (body, app) => {
  if (!windows.has(body)) {
    windows.set(body, new RowWindow(body, { columns: COLUMNS, indexAttr: 'data-idx', build: (i) => {
      const line = app.lines[i];
      const sel = selectionPredicate(app)(i);
      const flipped = takeEyeToggled(i);
      const row = rowOf(app, line, i, sel, flipped);
      row.dataset.sig = flipped ? '' : rowSig(app, line, i, sel);
      return row;
    } }));
  }
  return windows.get(body);
};

// One row per committed line, reflecting the selection. No-op while the Lines tab is hidden: the
// tab's own show renders it, so a skipped render never leaves it stale.
export const renderLinesList = (app) => {
  const table = document.getElementById('lines-list');
  const body = table?.tBodies?.[0];
  if (!table || !body || table.style.display === 'none') return;
  wireLinesList(body, table.parentElement ?? table, app);
  if (!app.lines.length) {
    body.replaceChildren();
    const row = document.createElement('tr');
    const message = cell('empty-message');
    message.colSpan = COLUMNS;
    message.textContent = 'No lines yet.';
    row.appendChild(message);
    body.appendChild(row);
    return;
  }
  if (windowOf(body, app).show(app.lines.length)) return;
  const selected = selectionPredicate(app);
  const old = [...body.children];
  app.lines.forEach((line, i) => {
    const sel = selected(i);
    const flipped = takeEyeToggled(i);
    const sig = rowSig(app, line, i, sel);
    if (!flipped && keepable(old[i], sig)) return;
    const row = rowOf(app, line, i, sel, flipped);
    row.dataset.sig = flipped ? '' : sig;   // a played flip is built again, without it, next time
    if (old[i]) body.replaceChild(row, old[i]); else body.appendChild(row);
  });
  for (let i = old.length - 1; i >= app.lines.length; i--) body.removeChild(old[i]);
};

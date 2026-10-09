// The "Lines" tab: one row per committed line — its number, its name, its own line colour and
// thickness, its point colour and size, its point count, its eye and its bin — on the points table's
// grid. The app keeps the state; this paints the rows. Desktop twin: app/selection/SelectionPanelLines.cpp.
import { icon } from '../../icons.js';
import { fillState } from '../../../core/layout.js';
import { pointColorOf } from '../../../core/line/render.js';
import { selectionPredicate } from '../../../core/line/selection.js';
import { wireLinesList, takeEyeToggled } from './events.js';
import { paintNameCell } from './nameEdit.js';
import constants from '../../../../../common/config/constants.json' with { type: 'json' };

const STROKE = constants.DEFAULT_VISUALS.color;
const { thickMin, thickMax, pointMin, pointMax } = constants.LIMITS;
const COLUMNS = 9;   // #, name, line colour, thickness, point colour, point size, points, eye, bin

const TIPS = Object.freeze({
  color: "Line color\nDouble-click: the toolbar's line color",
  area: "Line color — an area shows its fill inside\nDouble-click: the toolbar's line color",
  thickness: `Line thickness\nDouble-click to edit (${thickMin}–${thickMax} px)`,
  pointColor: "Point color\nDouble-click: the line's own color",
  pointSize: `Point size\nDouble-click to edit (${pointMin}–${pointMax} px)`,
  name: 'Line name\nDouble-click to rename',
});

// Class toggle only — the list is never scrolled by a canvas hover.
export const applyLinesListHover = (app) => {
  const el = document.getElementById('lines-list');
  if (!el) return;
  el.querySelectorAll('.lines-row').forEach(r => {
    r.classList.toggle('lines-row-hover', parseInt(r.dataset.idx, 10) === app.hoverLineIdx);
  });
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

const rowOf = (app, line, i, selected) => {
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
    eyeCell(line, i + 1, takeEyeToggled(i)), removeCell(i + 1));
  return row;
};

// One row per committed line, reflecting the selection. No-op while the Lines tab is hidden: the
// tab's own show renders it, so a skipped render never leaves it stale.
export const renderLinesList = (app) => {
  const table = document.getElementById('lines-list');
  const body = table?.tBodies?.[0];
  if (!table || !body || table.style.display === 'none') return;
  wireLinesList(body, table.parentElement ?? table, app);
  body.replaceChildren();
  if (!app.lines.length) {
    const row = document.createElement('tr');
    const message = cell('empty-message');
    message.colSpan = COLUMNS;
    message.textContent = 'No lines yet.';
    row.appendChild(message);
    body.appendChild(row);
    return;
  }
  const selected = selectionPredicate(app);
  app.lines.forEach((line, i) => body.appendChild(rowOf(app, line, i, selected(i))));
};

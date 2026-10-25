// The points table's rows (coordTable.js): built from a point, rewritten in place when the same
// line is listed again, and a moved point's cells repainted.
import { cmToUnit } from '../../utils.js';
import { icon } from '../icons.js';
import { pixelToPageCoords, pixelToPageCoordsAll } from '../../core/parse/pageMetrics.js';

export const buildCoordRow = (app, point, index) => {
  const pageCoords = pixelToPageCoords(app, point.x, point.y);
  const row = document.createElement('tr');
  row.dataset.ptIdx = index;
  // Focusable so a bare Delete/Backspace is scoped to this table, as the desktop's points
  // table scopes it to widget focus (SelectionPanel.cpp eventFilter).
  row.tabIndex = 0;
  if (index === app.focusedPtIdx) row.classList.add('row-focused');

  row.innerHTML = `
        <td>${index + 1}</td>
        <td class="cell-px-x">${Math.round(point.x)}</td>
        <td class="cell-px-y">${Math.round(point.y)}</td>
        <td>${cmToUnit(pageCoords.x, app.unit).toFixed(2)}</td>
        <td>${cmToUnit(pageCoords.y, app.unit).toFixed(2)}</td>
        <td style="text-align:center;padding:2px;"><button class="del-pt-btn btn-icon" data-title="Remove point">${icon('trash', { size: 14 })}</button></td>
      `;
  return row;
};

// A re-list keeps the rows already there, rewritten as buildCoordRow writes them (an open cell
// editor included), and adds or drops rows at the end; a row mid-exit means a full rebuild.
// Only a cell whose text moved is written: a re-list after one point's edit touches one row's DOM.
const setText = (el, text) => { if (el.textContent !== String(text)) el.textContent = text; };

export const reuseCoordRows = (app, body, points) => {
  const rows = [...body.querySelectorAll('tr[data-pt-idx]')];
  const settled = (r) => !r.getAttribute('style') && String(r.className).split(/\s+/).every((c) => !c || c === 'row-focused' || c === 'row-highlighted');
  if (!rows.length || rows.length !== (body.children ?? rows).length || !rows.every(settled)) return false;
  while (rows.length > points.length) rows.pop().remove();
  rows.forEach((row, index) => {
    const point = points[index];
    const pageCoords = pixelToPageCoords(app, point.x, point.y);
    if (row.dataset.ptIdx !== String(index)) row.dataset.ptIdx = index;
    row.classList.remove('row-highlighted');
    row.classList.toggle('row-focused', index === app.focusedPtIdx);
    const tds = row.children;
    setText(tds[0], index + 1);
    setText(tds[1], Math.round(point.x));
    setText(tds[2], Math.round(point.y));
    setText(tds[3], cmToUnit(pageCoords.x, app.unit).toFixed(2));
    setText(tds[4], cmToUnit(pageCoords.y, app.unit).toFixed(2));
  });
  for (let index = rows.length; index < points.length; index++) body.appendChild(buildCoordRow(app, points[index], index));
  return true;
};

// A windowed table's sizing row: each column's longest text over every point, laid out unseen
// (visibility: collapse), so the columns are as wide as the whole list would make them.
export const coordSizerRow = (app, points) => {
  const longest = ['', '', '', '', ''];
  const keep = (k, text) => { if (text.length > longest[k].length) longest[k] = text; };
  keep(0, String(points.length));
  const pages = pixelToPageCoordsAll(app, points);
  points.forEach((point, i) => {
    const pageCoords = pages[i];
    keep(1, String(Math.round(point.x)));
    keep(2, String(Math.round(point.y)));
    keep(3, cmToUnit(pageCoords.x, app.unit).toFixed(2));
    keep(4, cmToUnit(pageCoords.y, app.unit).toFixed(2));
  });
  const row = document.createElement('tr');
  row.style.visibility = 'collapse';
  row.setAttribute('aria-hidden', 'true');
  for (const text of [...longest, '']) row.appendChild(document.createElement('td')).textContent = text;
  return row;
};

export const paintCoordRow = (app, row, point) => {
  if (!point) return;
  const pageCoords = pixelToPageCoords(app, point.x, point.y);
  const cellX = row.querySelector('.cell-px-x');
  const cellY = row.querySelector('.cell-px-y');
  if (cellX && !cellX.querySelector('.coord-px-input')) cellX.textContent = Math.round(point.x);
  if (cellY && !cellY.querySelector('.coord-px-input')) cellY.textContent = Math.round(point.y);
  const tds = row.querySelectorAll('td');
  if (tds[3]) tds[3].textContent = cmToUnit(pageCoords.x, app.unit).toFixed(2);
  if (tds[4]) tds[4].textContent = cmToUnit(pageCoords.y, app.unit).toFixed(2);
};

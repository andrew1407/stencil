// The "Lines" tab list: the app keeps the state and the canvas redraw; this only paints rows.
import { icon } from '../icons.js';
import { leaveThenRemove } from '../motion.js';
import { fillState } from '../../core/layout.js';

// Class toggle only — the list is never scrolled by a canvas hover.
export const applyLinesListHover = (app) => {
  const el = document.getElementById('lines-list');
  if (!el) return;
  el.querySelectorAll('.lines-row').forEach(r => {
    r.classList.toggle('lines-row-hover', parseInt(r.dataset.idx, 10) === app.hoverLineIdx);
  });
};

const COLUMNS = 5;   // #, colour, line, points, remove — the header in panel/mainContent.js

// One row per committed line on the points table's own grid, reflecting the selection. No-op
// unless the Lines tab is showing, so the redraw/updateButtons hooks that call it stay cheap.
export const renderLinesList = (app) => {
  const table = document.getElementById('lines-list');
  const el = table?.tBodies?.[0];
  if (!table || !el || table.style.display === 'none') return;
  el.replaceChildren();
  if (!app.lines.length) {
    const row = document.createElement('tr');
    const cell = document.createElement('td');
    cell.colSpan = COLUMNS;
    cell.className = 'empty-message';
    cell.textContent = 'No lines yet.';
    row.appendChild(cell);
    el.appendChild(row);
    return;
  }
  app.lines.forEach((line, i) => {
    const row = document.createElement('tr');
    row.className = 'lines-row' + (app.isLineSelected(i) ? ' lines-row-selected' : '');
    if (i === app.hoverLineIdx) row.classList.add('lines-row-hover');
    row.dataset.idx = String(i);
    row.addEventListener('mouseenter', () => app.setListHoverLine(i));
    row.addEventListener('mouseleave', () => app.setListHoverLine(-1));

    const cell = (cls) => { const td = document.createElement('td'); if (cls) td.className = cls; return td; };
    const index = cell();
    index.textContent = String(i + 1);

    const swatchCell = cell('lines-swatch-cell');
    const swatch = document.createElement('span');
    swatch.className = 'lines-swatch';
    // What the line LOOKS like: its fill when it has one, its stroke otherwise — and the stroke
    // always as the rim, so a filled area still says which colour drew it.
    const fill = fillState(line, app.defaultFillColor);
    swatch.style.background = fill.enabled ? fill.value : (line.locked ? 'transparent' : (line.color || '#ffff00'));
    swatch.style.borderColor = line.color || '#ffff00';
    swatchCell.appendChild(swatch);

    const label = cell('lines-label');
    label.textContent = line.locked ? `Line ${i + 1} · area` : `Line ${i + 1}`;

    const count = cell('lines-count-cell');
    count.textContent = String(line.points.length);

    const rmCell = cell('lines-remove-cell');
    const rm = document.createElement('button');
    // The points table's own delete face: one bin, one look, both tabs.
    rm.className = 'lines-remove del-pt-btn btn-icon';
    rm.type = 'button';
    rm.dataset.title = 'Remove line';
    rm.setAttribute('aria-label', `Remove line ${i + 1}`);
    rm.innerHTML = icon('trash', { size: 13 });
    rm.addEventListener('click', (e) => {
      e.stopPropagation();
      if (app.compareReadOnly()) return; // read-only compare view
      leaveThenRemove(row, () => app.removeLine(i));
    });
    rmCell.appendChild(rm);

    row.addEventListener('click', (e) => {
      app.selectLineFromList(i, (e.ctrlKey || e.metaKey) && e.shiftKey);
    });
// Focusable so Delete/Backspace can be scoped to this list, like the points table's rows;
// the global Alt+Delete is untouched.
    row.tabIndex = 0;
    row.addEventListener('keydown', (e) => {
      if (e.key !== 'Delete' && e.key !== 'Backspace') return;
      if (app.compareReadOnly()) return; // read-only compare view
      e.preventDefault();
      e.stopPropagation();
      app.removeLine(i);
      const rows = el.querySelectorAll('.lines-row');
      if (rows.length) rows[Math.min(i, rows.length - 1)].focus();
    });
    row.append(index, swatchCell, label, count, rmCell);
    el.appendChild(row);
  });
};


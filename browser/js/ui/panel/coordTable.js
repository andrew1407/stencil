import { cmToUnit, isTypingTarget } from '../../utils.js';
import { icon } from '../icons.js';
import { leaveThenRemove } from '../motion.js';
import { pixelToPageCoords } from '../../core/parse/pageMetrics.js';
import { setPointCoord, removePoint } from '../../core/line/editOps.js';
// The points table DOM + per-row interactions.
export class CoordTable {
  #points = null;
  #lineIdx = -1;
  #wiredBody = null;

  constructor(app) {
    this.app = app;
  }

  // A bare update() re-renders the line the table targets; points alone are listed against the
  // line that owns them, so a delete in the table always acts on the line it shows.
  update(points, lineIdx) {
    if (points === undefined) [points, lineIdx] = this.#target();
    else if (lineIdx === undefined) lineIdx = this.#ownerOf(points);
    this.app.coordLineIdx = lineIdx;
    this.app.hoveredPtIdx = -1;
    this.#points = points;
    this.#lineIdx = lineIdx;
    this.#wire(this.app.coordinatesBody);
    this.app.coordinatesBody.innerHTML = '';

    if (!points || points.length === 0) {
      this.app.coordinatesBody.innerHTML = `<tr><td colspan="6" class="empty-message">No points yet.</td></tr>`;
      this.#capPanel();
      return;
    }

    points.forEach((point, index) => {
      const pageCoords = pixelToPageCoords(this.app, point.x, point.y);
      const row = document.createElement('tr');
      row.dataset.ptIdx = index;
      // Focusable so a bare Delete/Backspace is scoped to this table, as the desktop's points
      // table scopes it to widget focus (SelectionPanel.cpp eventFilter).
      row.tabIndex = 0;
      if (index === this.app.focusedPtIdx) row.classList.add('row-focused');

      row.innerHTML = `
        <td>${index + 1}</td>
        <td class="cell-px-x">${Math.round(point.x)}</td>
        <td class="cell-px-y">${Math.round(point.y)}</td>
        <td>${cmToUnit(pageCoords.x, this.app.unit).toFixed(2)}</td>
        <td>${cmToUnit(pageCoords.y, this.app.unit).toFixed(2)}</td>
        <td style="text-align:center;padding:2px;"><button class="del-pt-btn btn-icon" data-title="Remove point">${icon('trash', { size: 14 })}</button></td>
      `;
      this.app.coordinatesBody.appendChild(row);
    });
    this.#capPanel();
  }

  // -1 is the stroke in progress; an index past the line set names nothing.
  #target() {
    const i = this.app.coordLineIdx;
    const line = i === -1 ? this.app.currentLine : this.app.lines[i];
    return line ? [line.points, i] : [null, -1];
  }

  #ownerOf(points) {
    if (points && points === this.app.currentLine?.points) return -1;
    const i = points ? this.app.lines.findIndex((l) => l.points === points) : -1;
    return i >= 0 ? i : this.app.coordLineIdx;
  }

  // A drag moves the points the table already lists: rewrite the cells, keep the rows.
  refreshRows(points, lineIdx) {
    const rows = this.app.coordinatesBody.querySelectorAll('tr[data-pt-idx]');
    if (lineIdx !== this.#lineIdx || points !== this.#points || rows.length !== points.length) {
      this.update(points, lineIdx);
      return;
    }
    for (let i = 0; i < points.length; i++) this.refreshCoordRow(i);
  }

  // One listener per event type on the body, not per row; mouseover/out stand in for the
  // rows' enter/leave, ignoring moves between cells of the same row.
  #wire(body) {
    if (!body?.addEventListener || this.#wiredBody === body) return;
    this.#wiredBody = body;
    const rowOf = (e) => e.target?.closest?.('tr[data-pt-idx]') || null;
    const indexOf = (row) => parseInt(row.dataset.ptIdx, 10);
    const hover = (idx) => {
      this.app.hoveredPtIdx = idx;
      this.applyRowHighlight();
      this.app.renderer.redraw();
    };
    body.addEventListener('mouseover', e => {
      const row = rowOf(e);
      if (row && !row.contains(e.relatedTarget)) hover(indexOf(row));
    });
    body.addEventListener('mouseout', e => {
      const row = rowOf(e);
      if (row && !row.contains(e.relatedTarget)) hover(-1);
    });
    body.addEventListener('click', e => {
      const row = rowOf(e);
      if (!row) return;
      const index = indexOf(row);
      if (e.target.closest('.del-pt-btn')) {
        e.stopPropagation();
        if (this.app.compareReadOnly()) return;
        // Collapse the row away first; removePoint() rebuilds the table without it.
        const lineIdx = this.#lineIdx;
        leaveThenRemove(row, () => removePoint(this.app, lineIdx, index));
        return;
      }
      if (e.target.closest('.coord-px-input')) return;
      this.app.focusedPtIdx = (this.app.focusedPtIdx === index) ? -1 : index;
      this.applyRowHighlight();
      this.app.renderer.redraw();
    });
    // Delete/Backspace on a focused row removes that point (desktop parity); bare, because
    // focus scopes it. Skipped while a px cell is being edited.
    body.addEventListener('keydown', e => {
      const row = rowOf(e);
      if (!row || (e.key !== 'Delete' && e.key !== 'Backspace')) return;
      if (isTypingTarget(e.target)) return;
      if (this.app.compareReadOnly()) return;
      e.preventDefault();
      e.stopPropagation();
      const index = indexOf(row);
      removePoint(this.app, this.#lineIdx, index);
      this.focusRowAfterRemoval(index);
    });
    body.addEventListener('dblclick', e => {
      const cell = e.target?.closest?.('.cell-px-x, .cell-px-y');
      const row = cell && rowOf(e);
      if (row) this.#editCell(cell, cell.classList.contains('cell-px-x') ? 'x' : 'y', indexOf(row));
    });
  }

  #editCell(cell, axis, index) {
    if (this.app.compareReadOnly()) return;
    if (cell.querySelector('.coord-px-input')) return;
    const lineIdx = this.#lineIdx;
    const point = this.#points?.[index];
    if (!point) return;
    const curVal = Math.round(axis === 'x' ? point.x : point.y);
    cell.innerHTML = '';
    const inp = document.createElement('input');
    inp.type = 'number';
    inp.className = 'coord-px-input';
    inp.value = curVal;
    cell.appendChild(inp);
    inp.focus();
    inp.select();

    const commit = () => {
      const newVal = parseInt(inp.value, 10);
      if (!isNaN(newVal)) {
        setPointCoord(this.app, lineIdx, index, axis, newVal);
      } else {
        this.update(
          lineIdx === -1 ? (this.app.currentLine ? this.app.currentLine.points : null) : (this.app.lines[lineIdx] ? this.app.lines[lineIdx].points : null),
          lineIdx,
        );
      }
    };
    inp.addEventListener('blur', commit);
    inp.addEventListener('keydown', ev => {
      if (ev.key === 'Enter') inp.blur();
      if (ev.key === 'Escape')
        cell.textContent = curVal;
      if (ev.key === 'ArrowUp' || ev.key === 'ArrowDown') {
        ev.preventDefault();
        const step = ev.shiftKey ? 10 : 1;
        inp.value = parseInt(inp.value, 10) + (ev.key === 'ArrowUp' ? step : -step);
        const line = lineIdx === -1 ? this.app.currentLine : this.app.lines[lineIdx];
        if (line && !isNaN(parseInt(inp.value, 10))) {
          line.points[index][axis] = parseInt(inp.value, 10);
          this.app.renderer.redraw();
        }
      }
    });
  }

  // Re-cap the panel whenever the row count changes; guarded for contexts without the
  // panel (tests, the fullscreen clone).
  #capPanel() {
    try { this.app.zoomPan?.syncCoordPanelHeight?.(); } catch { /* no panel in this context */ }
  }

  // The table is rebuilt after a keyboard delete, so re-focus the row that slid into the
  // deleted one's place (Qt's table keeps its current row too).
  focusRowAfterRemoval(index) {
    const rows = this.app.coordinatesBody.querySelectorAll('tr[data-pt-idx]');
    if (!rows.length) return;
    (rows[Math.min(index, rows.length - 1)]).focus();
  }

  applyRowHighlight() {
    const rows = this.app.coordinatesBody.querySelectorAll('tr[data-pt-idx]');
    rows.forEach(r => {
      const i = parseInt(r.dataset.ptIdx, 10);
      r.classList.toggle('row-focused', i === this.app.focusedPtIdx);
      r.classList.toggle('row-highlighted', i === this.app.hoveredPtIdx && i !== this.app.focusedPtIdx);
    });
  }

  refreshCoordRow(ptIdx) {
    const lineIdx = this.app.coordLineIdx;
    const line = lineIdx === -1 ? this.app.currentLine : this.app.lines[lineIdx];
    if (!line || ptIdx >= line.points.length) return;
    const point = line.points[ptIdx];
    const row = this.app.coordinatesBody.querySelector(`tr[data-pt-idx="${ptIdx}"]`);
    if (!row) return;
    const pageCoords = pixelToPageCoords(this.app, point.x, point.y);
    const cellX = row.querySelector('.cell-px-x');
    const cellY = row.querySelector('.cell-px-y');
    if (cellX && !cellX.querySelector('.coord-px-input')) cellX.textContent = Math.round(point.x);
    if (cellY && !cellY.querySelector('.coord-px-input')) cellY.textContent = Math.round(point.y);
    const tds = row.querySelectorAll('td');
    if (tds[3]) tds[3].textContent = cmToUnit(pageCoords.x, this.app.unit).toFixed(2);
    if (tds[4]) tds[4].textContent = cmToUnit(pageCoords.y, this.app.unit).toFixed(2);
  }
}

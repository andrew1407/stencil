import { cmToUnit, isTypingTarget } from '../utils.js';
import { icon } from './icons.js';
import { leaveThenRemove } from './motion.js';
// The points table DOM + per-row interactions.
export class CoordTable {
  constructor(app) {
    this.app = app;
  }

  update(points = null, lineIdx = this.app.coordLineIdx) {
    this.app.coordLineIdx = lineIdx;
    this.app.hoveredPtIdx = -1;
    this.app.coordinatesBody.innerHTML = '';

    if (!points || points.length === 0) {
      this.app.coordinatesBody.innerHTML = `<tr><td colspan="6" class="empty-message">No points yet.</td></tr>`;
      this.#capPanel();
      return;
    }

    points.forEach((point, index) => {
      const pageCoords = this.app.pixelToPageCoords(point.x, point.y);
      const row = document.createElement('tr');
      row.dataset.ptIdx = index;
      // Focusable so a bare Delete/Backspace is scoped to this table, as the desktop's points
      // table scopes it to widget focus (selectionPanel.cpp eventFilter).
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

      row.addEventListener('mouseenter', () => {
        this.app.hoveredPtIdx = index;
        this.applyRowHighlight();
        this.app.renderer.redraw();
      });
      row.addEventListener('mouseleave', () => {
        this.app.hoveredPtIdx = -1;
        this.applyRowHighlight();
        this.app.renderer.redraw();
      });

      row.addEventListener('click', e => {
        if (e.target.closest('.del-pt-btn') || e.target.closest('.coord-px-input')) return;
        this.app.focusedPtIdx = (this.app.focusedPtIdx === index) ? -1 : index;
        this.applyRowHighlight();
        this.app.renderer.redraw();
      });

      // Delete/Backspace on a focused row removes that point (desktop parity); bare, because
      // focus scopes it. Skipped while a px cell is being edited.
      row.addEventListener('keydown', e => {
        if (e.key !== 'Delete' && e.key !== 'Backspace') return;
        if (isTypingTarget(e.target)) return;
        if (this.app.compareReadOnly()) return;
        e.preventDefault();
        e.stopPropagation();
        this.app.removePoint(lineIdx, index);
        this.focusRowAfterRemoval(index);
      });

      const makeEditable = (cell, axis) => {
        cell.addEventListener('dblclick', () => {
          if (this.app.compareReadOnly()) return;
          if (cell.querySelector('.coord-px-input')) return;
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
              this.app.setPointCoord(lineIdx, index, axis, newVal);
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
        });
      };
      makeEditable(row.querySelector('.cell-px-x'), 'x');
      makeEditable(row.querySelector('.cell-px-y'), 'y');

      row.querySelector('.del-pt-btn').addEventListener('click', e => {
        e.stopPropagation();
        if (this.app.compareReadOnly()) return;
        // Collapse the row away first; removePoint() rebuilds the table without it.
        leaveThenRemove(row, () => this.app.removePoint(lineIdx, index));
      });

      this.app.coordinatesBody.appendChild(row);
    });
    this.#capPanel();
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
      const i = parseInt(r.dataset.ptIdx);
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
    const pageCoords = this.app.pixelToPageCoords(point.x, point.y);
    const cellX = row.querySelector('.cell-px-x');
    const cellY = row.querySelector('.cell-px-y');
    if (cellX && !cellX.querySelector('.coord-px-input')) cellX.textContent = Math.round(point.x);
    if (cellY && !cellY.querySelector('.coord-px-input')) cellY.textContent = Math.round(point.y);
    const tds = row.querySelectorAll('td');
    if (tds[3]) tds[3].textContent = cmToUnit(pageCoords.x, this.app.unit).toFixed(2);
    if (tds[4]) tds[4].textContent = cmToUnit(pageCoords.y, this.app.unit).toFixed(2);
  }
}

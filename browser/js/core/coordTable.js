import { cmToUnit } from '../utils.js';
// ── CoordTable: the points table DOM + per-row interactions ──────
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
      return;
    }

    points.forEach((point, index) => {
      const pageCoords = this.app.pixelToPageCoords(point.x, point.y);
      const row = document.createElement('tr');
      row.dataset.ptIdx = index;
      if (index === this.app.focusedPtIdx) row.classList.add('row-focused');

      row.innerHTML = `
        <td>${index + 1}</td>
        <td class="cell-px-x">${Math.round(point.x)}</td>
        <td class="cell-px-y">${Math.round(point.y)}</td>
        <td>${cmToUnit(pageCoords.x, this.app.unit).toFixed(2)}</td>
        <td>${cmToUnit(pageCoords.y, this.app.unit).toFixed(2)}</td>
        <td style="text-align:center;padding:2px;"><button class="del-pt-btn" title="Remove point">🗑</button></td>
      `;

      // Hover → highlight row + canvas point
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

      // Click row → focus (highlight persistently)
      row.addEventListener('click', e => {
        if (e.target.closest('.del-pt-btn') || e.target.closest('.coord-px-input')) return;
        this.app.focusedPtIdx = (this.app.focusedPtIdx === index) ? -1 : index;
        this.applyRowHighlight();
        this.app.renderer.redraw();
      });

      // Double-click pixel X/Y cell → make editable
      const makeEditable = (cell, axis) => {
        cell.addEventListener('dblclick', () => {
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
              const line = lineIdx === -1 ? this.app.currentLine : this.app.lines[lineIdx];
              if (line) {
                line.points[index][axis] = newVal;
                this.app.saveHistory();
                this.app.renderer.redraw();
              }
            }
            this.update(
              lineIdx === -1 ? (this.app.currentLine ? this.app.currentLine.points : null) : (this.app.lines[lineIdx] ? this.app.lines[lineIdx].points : null),
              lineIdx,
            );
          };
          inp.addEventListener('blur', commit);
          inp.addEventListener('keydown', ev => {
            if (ev.key === 'Enter') inp.blur();
            if (ev.key === 'Escape')
              cell.textContent = curVal; // Revert
            if (ev.key === 'ArrowUp' || ev.key === 'ArrowDown') {
              ev.preventDefault();
              const step = ev.shiftKey ? 10 : 1;
              inp.value = parseInt(inp.value, 10) + (ev.key === 'ArrowUp' ? step : -step);
              // live preview
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

      // Delete point button
      row.querySelector('.del-pt-btn').addEventListener('click', e => {
        e.stopPropagation();
        const line = lineIdx === -1 ? this.app.currentLine : this.app.lines[lineIdx];
        if (!line) return;
        line.points.splice(index, 1);
        if (line.points.length === 0 && lineIdx !== -1) {
          this.app.lines.splice(lineIdx, 1);
          if (this.app.selectedLineIdx === lineIdx) this.app.deselectLine(false);
          this.app.coordLineIdx = -1;
          this.app.focusedPtIdx = -1;
          this.update(null);
        } else {
          if (this.app.focusedPtIdx >= line.points.length) this.app.focusedPtIdx = line.points.length - 1;
          this.update(line.points, lineIdx);
        }
        this.app.saveHistory();
        this.app.renderer.redraw();
        this.app.updateButtons();
      });

      this.app.coordinatesBody.appendChild(row);
    });
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
    // update page cells (in the active display unit)
    const tds = row.querySelectorAll('td');
    if (tds[3]) tds[3].textContent = cmToUnit(pageCoords.x, this.app.unit).toFixed(2);
    if (tds[4]) tds[4].textContent = cmToUnit(pageCoords.y, this.app.unit).toFixed(2);
  }
}

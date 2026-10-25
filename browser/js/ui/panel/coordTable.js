import { isTypingTarget, perFrame } from '../../utils.js';
import { leaveThenRemove } from '../motion/enterLeave.js';
import { buildCoordRow, reuseCoordRows, paintCoordRow, coordSizerRow } from './coordRows.js';
import { RowWindow } from './rowWindow.js';
import { setPointCoord, removePoint } from '../../core/line/editOps.js';
// The points table DOM + per-row interactions.
export class CoordTable {
  #points = null;
  #lineIdx = -1;
  #wiredBody = null;
  #window = null;   // a long line's rows as a window (rowWindow.js)
  // Pointer events land many times a frame; the rows are rewritten once a frame, newest points.
  #refreshSoon = perFrame((points, lineIdx) => {
    if (this.app.coordLineIdx === lineIdx) this.#refreshNow(points, lineIdx);
  });

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
    const body = this.app.coordinatesBody;
    this.#wire(body);

    if (!points || points.length === 0) {
      body.innerHTML = `<tr><td colspan="6" class="empty-message">No points yet.</td></tr>`;
      this.#capPanel();
      return;
    }
    if (this.#windowOf(body).show(points.length)) {
      this.#capPanel();
      return;
    }
    if (!reuseCoordRows(this.app, body, points)) {
      body.innerHTML = '';
      points.forEach((point, index) => body.appendChild(buildCoordRow(this.app, point, index)));
    }
    this.#capPanel();
  }



  #windowOf(body) {
    if (this.#window?.body !== body) {
      this.#window = new RowWindow(body, { columns: 6, indexAttr: 'data-pt-idx',
        build: (i) => buildCoordRow(this.app, this.#points[i], i), sizer: () => coordSizerRow(this.app, this.#points),
        onRender: () => this.applyRowHighlight() });
    }
    return this.#window;
  }

  // The row a canvas click picked, scrolled into view; a long line's window renders it on the way.
  revealRow(ptIdx) {
    if (this.#window?.active) { this.#window.reveal(ptIdx, 'smooth'); return; }
    const row = this.app.coordinatesBody.querySelector(`tr[data-pt-idx="${ptIdx}"]`);
    if (row && typeof row.scrollIntoView === 'function') row.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
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
  refreshRows(points, lineIdx) { this.#refreshSoon(points, lineIdx); }

  #refreshNow(points, lineIdx) {
    const rows = this.app.coordinatesBody.querySelectorAll('tr[data-pt-idx]');
    const listed = this.#window?.active ? this.#window.count : rows.length;
    if (lineIdx !== this.#lineIdx || points !== this.#points || listed !== points.length) {
      this.update(points, lineIdx);
      return;
    }
    for (const row of rows) paintCoordRow(this.app, row, points[parseInt(row.dataset.ptIdx, 10)]);
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
    if (this.#window?.active) {
      this.#window.reveal(Math.min(index, this.#window.count - 1))?.focus();
      return;
    }
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
    const row = this.app.coordinatesBody.querySelector(`tr[data-pt-idx="${ptIdx}"]`);
    if (row) paintCoordRow(this.app, row, line.points[ptIdx]);
  }

}

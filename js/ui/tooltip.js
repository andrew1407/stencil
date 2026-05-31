import { StencilElement, hostTag, define } from './base.js';
// ── Component: hover/coordinate tooltip ─────────────────────────
// Custom element that owns both its (dynamically filled) DOM and the
// show/hide/position logic that used to live in core/tooltip.js.
export class StencilTooltip extends StencilElement {
  app = null;

  static inner() { return ''; } // content is rendered on demand by show()/showLine()
  static template() { return hostTag('stencil-tooltip', 'id="tooltip" class="tooltip"', StencilTooltip.inner()); }

  // DrawingApp sets the app ref directly in its constructor; the ready event
  // is a backstop in case the element is wired before the app assigns it.
  wire(app) { this.app = app; }

  // Shared tooltip decision — used by mousemove and by Shift/Ctrl key-refresh
  // so the tooltip updates the instant a modifier is pressed (no need to re-hover).
  applyHover(clientX, clientY, x, y, mods) {
    if (mods.altKey) {
      this.hide();
      return;
    }
    // Ctrl held → show the live cursor-position coordinates
    if ((mods.ctrlKey || mods.metaKey) && !mods.shiftKey) {
      this.show(clientX, clientY, x, y);
      return;
    }
    const point = this.app.findNearestPoint(x, y);
    if (point) {
      this.show(clientX, clientY, point.x, point.y);
      return;
    }
    const lineIdx = this.app.findLineAt(x, y);
    if (lineIdx !== -1) this.showLine(clientX, clientY, this.app.lines[lineIdx], mods.shiftKey);
    else this.hide();
  }

  // Re-run the tooltip logic at the last known cursor position with given modifiers.
  // Lets Shift (full points) / Ctrl (cursor coords) tooltips appear immediately on keypress.
  refresh(mods) {
    if (!this.app.mouseOverCanvas || !this.app.image) return;
    if (this.app.isPanning || this.app.isDraggingPoint || this.app.isDraggingSegment ||
        this.app.isDraggingLine || this.app.isZoomRectDragging || this.app.isRectDrawDragging) return;
    const { x, y } = this.app.canvasCoords(this.app.lastMouseClientX, this.app.lastMouseClientY);
    this.applyHover(this.app.lastMouseClientX, this.app.lastMouseClientY, x, y, mods);
  }

  show(clientX, clientY, x, y) {
    if (!this.app.tooltipEnabled) {
      this.hide();
      return;
    }
    const pageCoords = this.app.pixelToPageCoords(x, y);
    const ps = this.app.getPageDimensions();
    const tailX = ps.width - pageCoords.x;
    const tailY = ps.height - pageCoords.y;

    const rows = [];
    if (this.app.tooltipShowScreen) rows.push(`
      <tr>
        <td><strong>Pixel</strong></td>
        <td>${Math.round(x)}</td>
        <td>${Math.round(y)}</td>
      </tr>`);
    if (this.app.tooltipShowPage) rows.push(`
      <tr>
        <td><strong>Page (cm)</strong></td>
        <td>${pageCoords.x.toFixed(2)}</td>
        <td>${pageCoords.y.toFixed(2)}</td>
      </tr>`);
    if (this.app.tooltipShowCoords) rows.push(`
      <tr>
        <td><strong>To edge (cm)</strong></td>
        <td>${tailX.toFixed(2)}</td>
        <td>${tailY.toFixed(2)}</td>
      </tr>`);

    if (rows.length === 0) {
      this.hide();
      return;
    }

    this.innerHTML = `
      <table class="tooltip-table">
        <thead>
          <tr>
            <th>Coordinate</th>
            <th>X</th>
            <th>Y</th>
          </tr>
        </thead>
        <tbody>${rows.join('')}
        </tbody>
      </table>
    `;
    this.style.display = 'block';
    this.position(clientX, clientY);
  }

  // Show a tooltip describing the hovered line:
  //   • default: just start + end points
  //   • Shift held: full points list
  showLine(clientX, clientY, line, showAll) {
    if (!this.app.tooltipEnabled) {
      this.hide();
      return;
    }
    if (!line || !line.points || line.points.length === 0) {
      this.hide();
      return;
    }
    const pts = line.points;
    const fmtRow = (label, p) => {
      const pc = this.app.pixelToPageCoords(p.x, p.y);
      return `<tr>
        <td><strong>${label}</strong></td>
        <td>${Math.round(p.x)}, ${Math.round(p.y)} px</td>
        <td>${pc.x.toFixed(2)}, ${pc.y.toFixed(2)} cm</td>
      </tr>`;
    };
    let bodyRows = '';
    let header = '';
    if (showAll || pts.length <= 2) {
      header = `<tr><th>#</th><th>Pixel</th><th>Page (cm)</th></tr>`;
      bodyRows = pts.map((p, i) => fmtRow(String(i + 1), p)).join('');
    } else {
      header = `<tr><th>Point</th><th>Pixel</th><th>Page (cm)</th></tr>`;
      bodyRows = fmtRow('Start', pts[0]) + fmtRow('End', pts[pts.length - 1]);
    }
    const hint = (!showAll && pts.length > 2)
      ? `<div style="padding:4px 10px 6px;font-size:11px;color:var(--text-muted);">Hold <strong>Shift</strong> for all ${pts.length} points</div>`
      : '';
    this.innerHTML = `<table class="tooltip-table">
      <thead>${header}</thead>
      <tbody>${bodyRows}</tbody>
    </table>${hint}`;
    this.style.display = 'block';
    this.position(clientX, clientY);
  }

  // Shared tooltip positioning helper used by both point + line tooltips
  position(clientX, clientY) {
    const tooltipRect = this.getBoundingClientRect();
    const viewportWidth = window.innerWidth;
    const viewportHeight = window.innerHeight;
    let left = clientX + 15;
    let top = clientY + 15;
    if (left + tooltipRect.width > viewportWidth) left = clientX - tooltipRect.width - 15;
    if (top + tooltipRect.height > viewportHeight) top = clientY - tooltipRect.height - 15;
    if (left < 0) left = 10;
    if (top < 0) top = 10;
    this.style.left = left + 'px';
    this.style.top = top + 'px';
  }

  hide() {
    this.style.display = 'none';
  }
}
define('stencil-tooltip', StencilTooltip);

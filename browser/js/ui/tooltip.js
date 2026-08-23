import { StencilElement, hostTag, define } from './base.js';
import { surfaceIn, surfaceOut, settleSurface } from './motion.js';
import { cmToUnit, unitLabel } from '../utils.js';
// ── Component: hover/coordinate tooltip ─────────────────────────
// Owns its dynamically-filled DOM and the show/hide/position logic.
export class StencilTooltip extends StencilElement {
  app = null;

  static inner() { return ''; } // content is rendered on demand by show()/showLine()
  static template() { return hostTag('stencil-tooltip', 'id="tooltip" class="tooltip"', StencilTooltip.inner()); }

  // DrawingApp sets the app ref directly in its constructor; the ready event
  // is a backstop in case the element is wired before the app assigns it.
  wire(app) {
    this.app = app;
    // Re-home the position:fixed tooltip to <body> so no ancestor transform (e.g. .container's reveal-animation identity matrix) becomes its containing block and offsets it from the cursor.
    if (this.parentElement !== document.body) document.body.appendChild(this);
  }

  // Shared tooltip decision — used by mousemove and by Shift/Ctrl key-refresh
  // so the tooltip updates the instant a modifier is pressed (no need to re-hover).
  applyHover(clientX, clientY, x, y, mods) {
    if (mods.altKey) {
      this.hide();
      return;
    }
    // In a comparison view only what the EDITED half actually shows may be labelled —
    // and each branch is judged by the coordinates it is about to display, not by where
    // the cursor happens to be (drawingApp.compareShowsPoint). Outside a comparison the
    // gate is always open, so this costs nothing in the normal case.
    const visible = (px, py) => this.app.compareShowsPoint(px, py);
    // Ctrl held → show the live cursor-position coordinates
    if ((mods.ctrlKey || mods.metaKey) && !mods.shiftKey) {
      if (visible(x, y)) this.show(clientX, clientY, x, y);
      else this.hide();
      return;
    }
    const point = this.app.findNearestPoint(x, y);
    if (point) {
      // The POINT's own coordinates decide: one just across the divider from the
      // pointer is not visible, however close the cursor is to it.
      if (visible(point.x, point.y)) this.show(clientX, clientY, point.x, point.y);
      else this.hide();
      return;
    }
    const lineIdx = this.app.findLineAt(x, y);
    // A line is hit-tested AT the cursor, so the cursor is the part of it being pointed
    // at — a line straddling the divider answers for its visible half only.
    if (lineIdx !== -1 && visible(x, y)) this.showLine(clientX, clientY, this.app.lines[lineIdx], mods.shiftKey);
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
    const u = this.app.unit;
    const lbl = unitLabel(u);
    const pageX = cmToUnit(pageCoords.x, u);
    const pageY = cmToUnit(pageCoords.y, u);
    const tailX = cmToUnit(ps.width - pageCoords.x, u);
    const tailY = cmToUnit(ps.height - pageCoords.y, u);

    const rows = [];
    if (this.app.tooltipShowScreen) rows.push(`
      <tr>
        <td><strong>Pixel</strong></td>
        <td>${Math.round(x)}</td>
        <td>${Math.round(y)}</td>
      </tr>`);
    if (this.app.tooltipShowPage) rows.push(`
      <tr>
        <td><strong>Page (${lbl})</strong></td>
        <td>${pageX.toFixed(2)}</td>
        <td>${pageY.toFixed(2)}</td>
      </tr>`);
    if (this.app.tooltipShowCoords) rows.push(`
      <tr>
        <td><strong>To edge (${lbl})</strong></td>
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
    this.reveal(clientX, clientY);
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
    const u = this.app.unit;
    const lbl = unitLabel(u);
    const fmtRow = (label, p) => {
      const pc = this.app.pixelToPageCoords(p.x, p.y);
      return `<tr>
        <td><strong>${label}</strong></td>
        <td>${Math.round(p.x)}, ${Math.round(p.y)} px</td>
        <td>${cmToUnit(pc.x, u).toFixed(2)}, ${cmToUnit(pc.y, u).toFixed(2)} ${lbl}</td>
      </tr>`;
    };
    let bodyRows = '';
    let header = '';
    if (showAll || pts.length <= 2) {
      header = `<tr><th>#</th><th>Pixel</th><th>Page (${lbl})</th></tr>`;
      bodyRows = pts.map((p, i) => fmtRow(String(i + 1), p)).join('');
    } else {
      header = `<tr><th>Point</th><th>Pixel</th><th>Page (${lbl})</th></tr>`;
      bodyRows = fmtRow('Start', pts[0]) + fmtRow('End', pts[pts.length - 1]);
    }
    const hint = (!showAll && pts.length > 2)
      ? `<div style="padding:4px 10px 6px;font-size:11px;color:var(--text-muted);">Hold <strong>Shift</strong> for all ${pts.length} points</div>`
      : '';
    this.innerHTML = `<table class="tooltip-table">
      <thead>${header}</thead>
      <tbody>${bodyRows}</tbody>
    </table>${hint}`;
    this.reveal(clientX, clientY);
  }

  // ── The readout is sand too ─────────────────────────────────────────────
  // Same flight as every other overlay (js/ui/motion.js surfaceIn/surfaceOut), out of —
  // and back into — the cursor it belongs to. Only on the none↔block edge: this tooltip
  // is re-rendered on every mousemove while it is up, and a burst per frame would be
  // both a mess and a cost. Short, because the cursor is already moving.
  static IN_MS = 240;
  static OUT_MS = 170;
  dust(clientX, clientY, enter) {
    const point = { x: clientX, y: clientY };
    if (enter ? !surfaceIn(this, point, { ms: StencilTooltip.IN_MS })
              : !surfaceOut(this, point, { ms: StencilTooltip.OUT_MS })) settleSurface(this);
  }

  // Reveal at `clientX/Y`, playing the gather only when it was not already showing.
  reveal(clientX, clientY) {
    const wasHidden = this.style.display !== 'block';
    this.style.display = 'block';
    this.position(clientX, clientY);
    if (wasHidden) this.dust(clientX, clientY, true);
  }

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
    // The box goes NOW; the cloud it leaves behind owns its own lifetime.
    if (this.style.display === 'block') {
      const r = this.getBoundingClientRect?.();
      if (r) this.dust(r.left + r.width / 2, r.top + r.height / 2, false);
    } else settleSurface(this);
    this.style.display = 'none';
  }
}
define('stencil-tooltip', StencilTooltip);

import { StencilElement, hostTag, define } from '../base.js';
import { surfaceIn, surfaceOut, settleSurface, TIP_SHOW_DELAY_MS } from '../motion.js';
import { cmToUnit, unitLabel } from '../../utils.js';
import { decideHover, refreshHover, scheduleReveal } from './tooltipHover.js';
// ── Component: hover/coordinate tooltip ─────────────────────────
// Owns its dynamically-filled DOM and the show/hide/position logic.
export class StencilTooltip extends StencilElement {
  app = null;

  // The shared tooltip wake-up delay (motion.js; controlTooltip.js rides it too).
  static SHOW_DELAY_MS = TIP_SHOW_DELAY_MS;
  showTimer = null;       // pending reveal, armed while the delay is running
  pendingKey = null;      // the target it's armed for
  pendingReveal = null;   // …and how to actually show it, once the delay elapses
  shownKey = null;        // the target CURRENTLY on screen (or about to be, mid-timer)

  static inner() { return ''; } // content is rendered on demand by show()/showLine()
  static template() { return hostTag('stencil-tooltip', 'id="tooltip" class="tooltip"', StencilTooltip.inner()); }

  // DrawingApp sets the app ref directly in its constructor; the ready event
  // is a backstop in case the element is wired before the app assigns it.
  wire(app) {
    this.app = app;
    // Re-home the position:fixed tooltip to <body> so no ancestor transform (e.g. .container's reveal-animation identity matrix) becomes its containing block and offsets it from the cursor.
    if (this.parentElement !== document.body) document.body.appendChild(this);
  }

  applyHover(clientX, clientY, x, y, mods, immediate = false) {
    decideHover(this, clientX, clientY, x, y, mods, immediate);
  }

  refresh(mods) { refreshHover(this, mods); }

  scheduleShow(key, revealFn, immediate) { scheduleReveal(this, key, revealFn, immediate); }

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

  // Only on the none↔block edge: this tooltip is re-rendered on every mousemove while it is up,
  // and a burst per frame would be both a mess and a cost.
  static IN_MS = 240;
  static OUT_MS = 170;
  dust(clientX, clientY, enter) {
    const point = { x: clientX, y: clientY };
    (enter ? surfaceIn : surfaceOut)(this, point,
      { ms: enter ? StencilTooltip.IN_MS : StencilTooltip.OUT_MS });
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
    // Drop the reveal delay along with the box — a target abandoned mid-wait must not
    // pop in late, describing whatever the cursor has since moved on to.
    clearTimeout(this.showTimer);
    this.showTimer = null;
    this.pendingKey = null;
    this.pendingReveal = null;
    this.shownKey = null;
    // The box goes NOW; the cloud it leaves behind owns its own lifetime.
    if (this.style.display === 'block') {
      const r = this.getBoundingClientRect?.();
      if (r) this.dust(r.left + r.width / 2, r.top + r.height / 2, false);
    } else settleSurface(this);
    this.style.display = 'none';
  }
}
define('stencil-tooltip', StencilTooltip);

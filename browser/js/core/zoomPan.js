// ── ZoomPan: zoom level, fit, hold-zoom, overlays ───────────────
export class ZoomPan {
  constructor(app) {
    this.app = app;
  }

  updateZoomRectOverlay() {
    const overlay = document.getElementById('zoomRectOverlay');
    if (!overlay || !this.app.zoomRectStart || !this.app.zoomRectEnd) return;
    const s = this.app.zoomRectStart;
    const en = this.app.zoomRectEnd;
    const x1 = Math.min(s.cssX, en.cssX);
    const y1 = Math.min(s.cssY, en.cssY);
    const w = Math.abs(en.cssX - s.cssX);
    const h = Math.abs(en.cssY - s.cssY);
    overlay.style.left = x1 + 'px';
    overlay.style.top = y1 + 'px';
    overlay.style.width = w + 'px';
    overlay.style.height = h + 'px';
    overlay.style.display = 'block';
  }

  hideZoomRectOverlay() {
    const overlay = document.getElementById('zoomRectOverlay');
    if (overlay) overlay.style.display = 'none';
  }

  // Reuse the zoom-rect overlay element to preview a rectangle being drawn.
  updateRectDrawOverlay() {
    const overlay = document.getElementById('zoomRectOverlay');
    if (!overlay || !this.app.rectDrawStart || !this.app.rectDrawEnd) return;
    const s = this.app.rectDrawStart;
    const en = this.app.rectDrawEnd;
    overlay.style.left = Math.min(s.cssX, en.cssX) + 'px';
    overlay.style.top = Math.min(s.cssY, en.cssY) + 'px';
    overlay.style.width = Math.abs(en.cssX - s.cssX) + 'px';
    overlay.style.height = Math.abs(en.cssY - s.cssY) + 'px';
    overlay.style.display = 'block';
  }

  // Update every zoom-percent input on the page (the original plus any
  // fullscreen clones still in the DOM). Skip the one the user is
  // currently editing so typing isn't interrupted.
  setZoomInputValue(percent) {
    const inputs = document.querySelectorAll('[id="zoomInput"]');
    inputs.forEach(el => {
      if (document.activeElement === el) return;
      el.value = percent;
    });
  }

  setZoom(newScale, persist = true) {
    newScale = Math.max(0.05, Math.min(5, newScale));
    this.app.scale = newScale;
    this.app.canvas.style.width = (this.app.canvas.width * newScale) + 'px';
    this.app.canvas.style.height = (this.app.canvas.height * newScale) + 'px';
    this.setZoomInputValue(Math.round(newScale * 100));
    // Persist zoom level (scroll is saved via the debounced scroll listener)
    if (persist && this.app.image) this.app.storage.save();
  }

  // Press-and-hold zoom for the +/− buttons.
  //   • Single press → small step (0.25)
  //   • Double-press → large step (1.0)  — second mousedown within 280 ms
  //   • Hold        → continuous zoom (kicks in after 380 ms, then 0.10 every 70 ms)
  // sign is +1 for zoom-in, −1 for zoom-out.
  setupHoldZoom(btn, sign) {
    // Smaller, gentler steps so a single click feels like one notch, not a leap.
    const SMALL = 0.10;
    const LARGE = 0.40;
    const CONT = 0.05;
    const DBL_WINDOW = 280;
    const HOLD_DELAY = 480;
    const REPEAT_MS = 90;
    let holdTimer = null;
    let repeatTimer = null;
    let lastPress = 0;
    const stop = () => {
      clearTimeout(holdTimer);
      holdTimer = null;
      clearInterval(repeatTimer);
      repeatTimer = null;
    };
    btn.addEventListener('mousedown', e => {
      if (e.button !== 0) return;
      e.preventDefault();
      const now = performance.now();
      const isDouble = (now - lastPress) < DBL_WINDOW;
      lastPress = isDouble ? 0 : now;
      // On double-click, top up the prior SMALL step to reach LARGE in total
      const step = isDouble ? (LARGE - SMALL) : SMALL;
      const target = Math.max(0.05, Math.min(5, this.app.scale + sign * step));
      // Update the zoom % input synchronously so users see immediate feedback
      this.setZoomInputValue(Math.round(target * 100));
      this.zoomAroundCenter(target);
      holdTimer = setTimeout(() => {
        repeatTimer = setInterval(() => {
          const t = Math.max(0.05, Math.min(5, this.app.scale + sign * CONT));
          this.setZoomInputValue(Math.round(t * 100));
          this.zoomAroundCenter(t);
        }, REPEAT_MS);
      }, HOLD_DELAY);
    });
    btn.addEventListener('mouseup', stop);
    btn.addEventListener('mouseleave', stop);
    // If the mouse is released anywhere in the window, also stop
    window.addEventListener('mouseup', stop);
  }

  // this.app.renderedScale tracks the scale that is actually on screen right now;
  // this.app.scale is kept in sync each frame so rapid clicks start from the
  // correct visual position rather than the stale logical target.
  zoomAroundCenter(newScale) {
    const vp = document.getElementById('canvasViewport');
    if (!vp || !this.app.image) {
      this.setZoom(newScale);
      return;
    }
    newScale = Math.max(0.05, Math.min(5, newScale));

    // Cancel any in-flight animation; start from whatever is on screen NOW
    if (this.app.zoomAnimRaf) {
      cancelAnimationFrame(this.app.zoomAnimRaf);
      this.app.zoomAnimRaf = null;
    }

    // scaleStart = what the canvas actually looks like right now
    const scaleStart = (this.app.renderedScale != null) ? this.app.renderedScale : this.app.scale;
    const scrollX0 = vp.scrollLeft;
    const scrollY0 = vp.scrollTop;

    // Image-space point at current viewport center (use scaleStart, not this.app.scale)
    const imgCx = (scrollX0 + vp.clientWidth / 2) / scaleStart;
    const imgCy = (scrollY0 + vp.clientHeight / 2) / scaleStart;

    const scrollX1 = imgCx * newScale - vp.clientWidth / 2;
    const scrollY1 = imgCy * newScale - vp.clientHeight / 2;

    // Suppress CSS transition — we control every frame ourselves
    this.app.canvas.classList.add('zoom-no-transition');

    const DURATION = 200; // ms
    const ease = t => 1 - (1 - t) * (1 - t); // ease-out-quad

    const t0 = performance.now();
    const tick = now => {
      const p = Math.min((now - t0) / DURATION, 1);
      const e = ease(p);

      // Interpolated scale — keep this.app.scale in sync so hit-tests are correct
      const s = scaleStart + (newScale - scaleStart) * e;
      this.app.renderedScale = s;
      this.app.scale = s;

      // Write canvas CSS size first — scrollWidth grows with it
      this.app.canvas.style.width = (this.app.canvas.width * s) + 'px';
      this.app.canvas.style.height = (this.app.canvas.height * s) + 'px';

      // Write scroll — never clamped because scrollWidth just grew
      vp.scrollLeft = scrollX0 + (scrollX1 - scrollX0) * e;
      vp.scrollTop = scrollY0 + (scrollY1 - scrollY0) * e;

      this.setZoomInputValue(Math.round(s * 100));

      if (p < 1) {
        this.app.zoomAnimRaf = requestAnimationFrame(tick);
      } else {
        // Snap to exact final values
        this.app.scale = newScale;
        this.app.renderedScale = newScale;
        this.app.canvas.style.width = (this.app.canvas.width * newScale) + 'px';
        this.app.canvas.style.height = (this.app.canvas.height * newScale) + 'px';
        vp.scrollLeft = scrollX1;
        vp.scrollTop = scrollY1;
        this.setZoomInputValue(Math.round(newScale * 100));
        this.app.canvas.classList.remove('zoom-no-transition');
        this.app.zoomAnimRaf = null;
        if (this.app.image) this.app.storage.save();
      }
    };
    this.app.zoomAnimRaf = requestAnimationFrame(tick);
  }

  fitToWindow() {
    if (!this.app.image) return;
    const isFS = document.body.classList.contains('fullscreen-mode');
    // In fullscreen the side panel and toolbar are hidden — use full window dimensions
    const availW = isFS ? window.innerWidth : Math.max(300, window.innerWidth - 420);
    const availH = isFS ? window.innerHeight : Math.max(200, window.innerHeight - 220);
    const scaleW = availW / this.app.image.width;
    const scaleH = availH / this.app.image.height;
    const fit = Math.min(scaleW, scaleH, 1); // never upscale beyond 100% on fit
    this.setZoom(Math.round(fit * 100) / 100); // round to 2 decimals

    // Size viewport to show the fitted image fully (cap at availH)
    const viewport = document.getElementById('canvasViewport');
    if (viewport) {
      if (!isFS) {
        // Normal mode: shrink viewport to fit image snugly
        const fittedH = Math.round(this.app.image.height * this.app.scale);
        viewport.style.maxHeight = Math.min(fittedH + 4, availH) + 'px';
      }
      // In fullscreen: viewport maxHeight/maxWidth are already set by toggleFullscreen;
      // don't override them or the image will be clipped again
      // Reset scroll to top-left on fit
      viewport.scrollLeft = 0;
      viewport.scrollTop = 0;
    }
  }
}

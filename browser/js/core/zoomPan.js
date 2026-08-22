import { core } from './stencilCore.js';

// ── ZoomPan: zoom level, fit, hold-zoom, overlays ───────────────
// Smallest canvas viewport we will size down to before letting the page scroll.
const MIN_VIEWPORT_H = 120;
// …and the same floor for the coordinates panel: below this the header + a row or two
// would be clipped, and on a window that short something has to give anyway.
const MIN_PANEL_H = 120;

// Bottom padding + border + margin of every ancestor from el's parent up through <body>.
// Used by availContentHeight() — counting just one level (body) over-reports the room
// and leaves a permanent page scrollbar.
const bottomInsetToPage = (el) => {
  let total = 0;
  for (let n = el; n && n !== document.documentElement; n = n.parentElement) {
    const c = getComputedStyle(n);
    total += (parseFloat(c.marginBottom) || 0);
    if (n !== el) total += (parseFloat(c.paddingBottom) || 0) + (parseFloat(c.borderBottomWidth) || 0);
  }
  return total;
};

// What sits under the viewport inside its own column (status line + drop hint), summed
// from the siblings themselves: the column is a stretched flex box (layout.css), so
// measuring to the column's bottom EDGE counts leftover slack as occupied space.
const belowInColumn = (vp) => {
  let total = 0;
  for (let n = vp.nextElementSibling; n; n = n.nextElementSibling) {
    const c = getComputedStyle(n);
    if (c.display === 'none') continue;
    total += n.getBoundingClientRect().height + (parseFloat(c.marginTop) || 0) + (parseFloat(c.marginBottom) || 0);
  }
  return total;
};

// Where the image starts inside the viewport's scrollable content. The canvas is centred
// with auto margins (layout.css .canvas-container), so while it is SMALLER than the frame
// its origin is no longer the scroll origin — every viewport→image conversion has to take
// this off first, or a zoom focal point lands half a viewport away from the cursor.
// Measured, so it needs no knowledge of scrollbars or the frame; 0 once the canvas
// overflows, which is also the only time scrolling exists. That is why the reverse
// direction (image→scroll) needs no term: the margins are 0 exactly when a scroll offset
// can be non-zero, and any other write is clamped to 0 — i.e. to the centred state.
export const canvasOrigin = () => {
  const c = typeof document !== 'undefined' && document.getElementById('canvas-container');
  return { x: (c && c.offsetLeft) || 0, y: (c && c.offsetTop) || 0 };
};

// How long after the LAST zoom step the session is persisted. Trailing-edge: a
// wheel/hold burst writes the final zoom once — one save, one "Saved" toast — instead
// of a full layout + thumbnail write per notch.
export const ZOOM_SAVE_DEBOUNCE_MS = 400;

// Trailing-edge debounce, timers injectable so the collapse is unit-testable. Only the
// last call in a burst runs `run`, after `delay` of quiet; `flush()` runs a pending
// save NOW, `pending()` answers whether one is armed.
export const createTrailingSave = (run, { delay = ZOOM_SAVE_DEBOUNCE_MS,
                                          setTimer = setTimeout, clearTimer = clearTimeout } = {}) => {
  let timer = null;
  const fire = () => { timer = null; run(); };
  const call = () => {
    if (timer !== null) clearTimer(timer);
    timer = setTimer(fire, delay);
  };
  call.flush = () => { if (timer === null) return; clearTimer(timer); fire(); };
  call.pending = () => timer !== null;
  return call;
};

export class ZoomPan {
  constructor(app) {
    this.app = app;
    // The single debounced persistence path for every zoom route (wheel/hold steps via
    // setZoom, the animated zoom's final snap): a burst saves once, at its end.
    this.persistZoom = createTrailingSave(() => { if (this.app.image) this.app.storage.save(); });
  }

  // Clamp a scale into the zoom limits. Delegates to the shared C++ core (wasm)
  // clampScale when loaded; the JS bound is the reference + fallback.
  clampScale = core.bind('clampScale', s => Math.max(0.05, Math.min(32, s)));

  updateZoomRectOverlay() {
    const overlay = document.getElementById('zoom-rect-overlay');
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
    const overlay = document.getElementById('zoom-rect-overlay');
    if (overlay) overlay.style.display = 'none';
  }

  // Reuse the zoom-rect overlay element to preview a rectangle being drawn.
  updateRectDrawOverlay() {
    const overlay = document.getElementById('zoom-rect-overlay');
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
    const inputs = document.querySelectorAll('[id="zoom-input"]');
    inputs.forEach(el => {
      if (document.activeElement === el) return;
      el.value = percent;
    });
  }

  // Available height for the canvas viewport in normal mode: from the viewport's own top
  // edge to the window bottom, minus the status line + drop hint below it. Measured live
  // so it tracks the real toolbar height and resizes; fullscreen uses the whole window.
  availContentHeight() {
    if (document.body.classList.contains('fullscreen-mode')) return window.innerHeight;
    const vp = document.getElementById('canvas-viewport');
    if (!vp) return Math.max(200, window.innerHeight - 140 - 96);
    const r = vp.getBoundingClientRect();
    // What sits BELOW the viewport is MEASURED, never assumed — a fixed guess leaves a
    // permanent scrollbar. Off the viewport's own column, NOT .container: that also
    // encloses the coordinates panel, which grows with the point list.
    const shell = vp.closest('.canvas-section') || vp.parentElement;
    // Everything between the shell's bottom edge and the page bottom counts too: each
    // ancestor up to <body> contributes its own bottom padding/border/margin; summing
    // only body's leaves the page tall enough for a permanent window scrollbar.
    const below = shell ? belowInColumn(vp) + bottomInsetToPage(shell) : 96;
    // The floor must stay below ordinary window heights so the true fit wins; it only
    // bites on a genuinely tiny window, where something has to give anyway.
    return Math.max(MIN_VIEWPORT_H, Math.floor(window.innerHeight - r.top - below));
  }

  // Measured off the viewport itself for the same reason availContentHeight() is: a fixed
  // inset guess drifts from the real box. clientWidth already excludes a vertical
  // scrollbar; fullscreen uses the whole window width.
  availContentWidth() {
    if (document.body.classList.contains('fullscreen-mode')) return window.innerWidth;
    const vp = document.getElementById('canvas-viewport');
    const w = vp ? vp.clientWidth : 0;
    return w > 0 ? w : Math.max(300, window.innerWidth - 420);
  }

  // Vertical border + padding of the viewport. It is a border-box element with a 2px frame,
  // so its height budget (availContentHeight) includes room the image cannot use — count
  // it once here rather than as a magic constant in the two places that need it.
  viewportChromeY() {
    const vp = document.getElementById('canvas-viewport');
    if (!vp) return 0;
    const cs = getComputedStyle(vp);
    return ['borderTopWidth', 'borderBottomWidth', 'paddingTop', 'paddingBottom']
      .reduce((n, k) => n + (parseFloat(cs[k]) || 0), 0);
  }

  // canvasOrigin() for a zoom level that is not on screen yet: half the free space, or
  // nothing once the image outgrows the frame — the rule the auto margins follow.
  originAt(scale) {
    const vp = document.getElementById('canvas-viewport');
    if (!vp || !this.app.canvas) return { x: 0, y: 0 };
    return {
      x: Math.max(0, ((vp.clientWidth || 0) - this.app.canvas.width * scale) / 2),
      y: Math.max(0, ((vp.clientHeight || 0) - this.app.canvas.height * scale) / 2),
    };
  }

  // The frame ALWAYS takes the whole available height — with no image, with a small one, and
  // at any zoom — and the picture is centred inside it by the auto margins (layout.css).
  // It used to hug the image instead, which collapsed the frame to a short strip whenever
  // the picture was small or zoomed out (user report, with screenshots).
  //
  // The cap is now the AVAILABLE height, not the image's: `flex: 1 1 auto` (layout.css)
  // fills the column up to it, so the frame is full height without a pixel height pinning
  // it — which is what lets it follow a toolbar fold smoothly instead of jumping at the end.
  // The cap is still what stops a zoomed-in canvas from stretching the page instead of
  // scrolling. Scale-independent: nothing here has to run per zoom step. No-op in
  // fullscreen, where the layer owns the box (components.css pins it to the window).
  syncViewportHeight() {
    const vp = document.getElementById('canvas-viewport');
    if (!vp || document.body.classList.contains('fullscreen-mode')) return;
    vp.style.maxHeight = this.availContentHeight() + 'px';
  }

  // Cap the coordinates panel to the room it actually has, so a long point list scrolls
  // INSIDE it (#coord-body is overflow-y:auto) instead of stretching the page. Measured
  // off the panel's own LIVE top — it starts below the toolbar, so a vh guess leaves a
  // permanent scrollbar; sticky is fine (a fitted panel never scrolls, resize re-measures).
  syncCoordPanelHeight() {
    const panel = document.getElementById('coord-panel');
    if (!panel || document.body.classList.contains('fullscreen-mode')) return;
    const top = panel.getBoundingClientRect().top;
    const avail = Math.floor(window.innerHeight - top - bottomInsetToPage(panel));
    panel.style.maxHeight = Math.max(MIN_PANEL_H, avail) + 'px';
  }

  setZoom(newScale, persist = true) {
    // No image → there's nothing to scale; ignore zoom requests entirely.
    if (!this.app.image) return;
    newScale = this.clampScale(newScale);
    this.app.scale = newScale;
    // This IS what goes on screen, so the on-screen tracker moves with it: left stale from
    // an older animated zoom, the next focal zoom measures its start from a scale the
    // canvas no longer has and lands somewhere else entirely.
    this.app.renderedScale = newScale;
    this.app.canvas.style.width = (this.app.canvas.width * newScale) + 'px';
    this.app.canvas.style.height = (this.app.canvas.height * newScale) + 'px';
    // Not for the zoom (the frame is full-height at every scale) but for the room: a toolbar
    // that reflowed moves the viewport's top, and this is the cheapest place to catch it.
    this.syncViewportHeight();
    this.setZoomInputValue(Math.round(newScale * 100));
    // Persist zoom level — debounced (createTrailingSave): a wheel/hold burst writes
    // once, at its end, instead of a full save + "Saved" toast per step. (Scroll is
    // saved via the debounced scroll listener on the same principle.)
    if (persist && this.app.image) this.persistZoom();
  }

  // Press-and-hold zoom for the +/− buttons. sign is +1 zoom-in, −1 zoom-out.
  // Single press → small step; double-press → large step; hold → continuous zoom.
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
      const target = this.clampScale(this.app.scale + sign * step);
      // Update the zoom % input synchronously so users see immediate feedback
      this.setZoomInputValue(Math.round(target * 100));
      this.zoomAroundCenter(target);
      holdTimer = setTimeout(() => {
        repeatTimer = setInterval(() => {
          const t = this.clampScale(this.app.scale + sign * CONT);
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
    if (!this.app.image) return;
    const vp = document.getElementById('canvas-viewport');
    if (!vp) {
      this.setZoom(newScale);
      return;
    }
    newScale = this.clampScale(newScale);
    // No viewport resize here any more: the frame is full-height at every zoom, so the
    // clientHeight the centring math reads below is already the one the zoom lands in.

    // Cancel any in-flight animation; start from whatever is on screen NOW
    if (this.app.zoomAnimRaf) {
      cancelAnimationFrame(this.app.zoomAnimRaf);
      this.app.zoomAnimRaf = null;
    }

    // scaleStart = what the canvas actually looks like right now
    const scaleStart = (this.app.renderedScale != null) ? this.app.renderedScale : this.app.scale;
    const scrollX0 = vp.scrollLeft;
    const scrollY0 = vp.scrollTop;

    // Image-space point at current viewport center (use scaleStart, not this.app.scale).
    const o0 = canvasOrigin();
    const imgCx = (scrollX0 + vp.clientWidth / 2 - o0.x) / scaleStart;
    const imgCy = (scrollY0 + vp.clientHeight / 2 - o0.y) / scaleStart;

    // Predicted centring margin at the target zoom (0 once the image overflows), so a zoom
    // that ends up smaller than the frame targets 0 exactly instead of a clamped guess.
    const o1 = this.originAt(newScale);
    const scrollX1 = imgCx * newScale - vp.clientWidth / 2 + o1.x;
    const scrollY1 = imgCy * newScale - vp.clientHeight / 2 + o1.y;

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
        if (this.app.image) this.persistZoom();
      }
    };
    this.app.zoomAnimRaf = requestAnimationFrame(tick);
  }

  // Zoom to `newScale` keeping the image-space point (imgX, imgY) pinned under its
  // current on-screen position (focal zoom). Non-animated; used by the console API's
  // stencil.zoom(amount, { x, y }). Falls back to plain setZoom with no viewport.
  zoomToImagePoint(newScale, imgX, imgY) {
    if (!this.app.image) return;
    newScale = this.clampScale(newScale);
    const vp = document.getElementById('canvas-viewport');
    if (!vp) { this.setZoom(newScale); return; }
    if (this.app.zoomAnimRaf) { cancelAnimationFrame(this.app.zoomAnimRaf); this.app.zoomAnimRaf = null; }
    const scaleStart = (this.app.renderedScale != null) ? this.app.renderedScale : this.app.scale;
    // On-screen offset (within the viewport) of the focal point right now — through the
    // centring margins, which move the image origin off the scroll origin.
    const o0 = canvasOrigin();
    const offX = imgX * scaleStart + o0.x - vp.scrollLeft;
    const offY = imgY * scaleStart + o0.y - vp.scrollTop;
    // The canvas CSS size TRANSITIONS (layout.css), so mid-flight the scroll range is
    // still the old one and the browser clamps the write below — the focal point slid.
    // Suppress it for this step (the animated zoom does the same), and let it back on a
    // frame later, once the new size is settled and nothing is left to animate.
    this.app.canvas.classList.add('zoom-no-transition');
    this.setZoom(newScale);          // updates scale + canvas CSS size + zoom input + persist
    this.app.renderedScale = newScale;
    const o1 = canvasOrigin();       // re-measured (and flushes the new size into layout)
    vp.scrollLeft = imgX * newScale + o1.x - offX;
    vp.scrollTop = imgY * newScale + o1.y - offY;
    requestAnimationFrame(() => this.app.canvas.classList.remove('zoom-no-transition'));
  }

  fitToWindow() {
    if (!this.app.image) return;
    // Fit against the box the image actually lands in — the SAME measurements
    // syncViewportHeight() sizes the viewport to; fixed insets over-estimate the room
    // and clip the fitted image. The height budget is border-box, so take the frame off.
    const availW = this.availContentWidth();
    const availH = Math.max(1, this.availContentHeight() - this.viewportChromeY());
    const scaleW = availW / this.app.image.width;
    const scaleH = availH / this.app.image.height;
    const fit = Math.min(scaleW, scaleH, 1); // never upscale beyond 100% on fit
    // Round DOWN to the same 1% the zoom input shows: rounding up re-introduces the
    // overflow this fit exists to avoid (619px at 0.7754 → 0.78 → 3px clipped).
    this.setZoom(Math.floor(fit * 100) / 100);

    // Viewport height is the available height either way (setZoom → syncViewportHeight, or
    // the fullscreen layer's own rule) — nothing to size here, just the scroll reset.
    const viewport = document.getElementById('canvas-viewport');
    if (viewport) {
      // Reset scroll to top-left on fit
      viewport.scrollLeft = 0;
      viewport.scrollTop = 0;
    }
  }
}

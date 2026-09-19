import { canvasOrigin } from '../../core/zoomPan.js';
export function wireSmoothZoom(app) {
  // Rapid wheel events accumulate into one rAF loop. Add `zoom-no-transition` while it runs, or
  // the CSS width/height transition fights it and flickers.
  const smoothZoom = { target: null, focal: null, rafId: null };

  const viewport = document.getElementById('canvas-viewport');

  const runSmoothZoom = () => {
    const sz = smoothZoom;
    const oldScale = app.scale;
    const diff = sz.target - oldScale;

    if (Math.abs(diff) < 0.0018) {
      // Snap to final target, re-enable CSS transition, persist
      app.canvas.classList.remove('zoom-no-transition');
      app.zoomPan.setZoom(sz.target, true);
      // Apply final focal-point scroll after snap
      if (sz.focal && viewport) {
        const { imgX, imgY, clientX, clientY } = sz.focal;
        const org = app.zoomPan.originAt(sz.target);
        viewport.scrollLeft = imgX * sz.target + org.x - clientX;
        viewport.scrollTop = imgY * sz.target + org.y - clientY;
      }
      sz.rafId = null;
      sz.focal = null;
      sz.target = null;
      return;
    }

    // Ease towards target (~0.25 per frame — smooth but responsive)
    const next = oldScale + diff * 0.25;
    // Set scale directly (CSS transition is OFF — no conflict)
    app.scale = next;
    app.canvas.style.width = (app.canvas.width  * next) + 'px';
    app.canvas.style.height = (app.canvas.height * next) + 'px';
    // No viewport resize per frame: the frame is full-height at every zoom now.
    app.zoomPan.setZoomInputValue(Math.round(next * 100));

    // Keep the image pixel under the cursor fixed, through the centring margins at THIS scale
    // (originAt): they collapse to 0 exactly when scrolling starts, so the pixel stays put.
    if (sz.focal && viewport) {
      const { imgX, imgY, clientX, clientY } = sz.focal;
      const org = app.zoomPan.originAt(next);
      viewport.scrollLeft = imgX * next + org.x - clientX;
      viewport.scrollTop = imgY * next + org.y - clientY;
    }

    sz.rafId = requestAnimationFrame(runSmoothZoom);
  };

  // Ctrl+wheel (also fired by touchpad pinch-to-zoom) OR Alt+wheel → zoom toward cursor.
  // The +/− buttons use zoomAroundCenter() instead, so center-zoom is still reachable.
  document.addEventListener('wheel', e => {
    if (!e.ctrlKey && !e.altKey && !e.metaKey) return;
    // No image → nothing to zoom/thicken/rotate; let plain scroll pass through.
    if (!app.image) return;
    // Only act when the wheel is over the canvas viewport — otherwise Ctrl/Alt+wheel over a
    // panel, the projects list, the coord table, etc. would hijack the scroll to zoom the canvas.
    if (!viewport || !viewport.contains(e.target)) return;

    // Alt+wheel → adjust thickness of the line under the cursor
    // (point's line if hovering a point, else the hovered line). Read-only while comparing.
    if (e.altKey && !e.ctrlKey && !e.metaKey && !app.compareReadOnly()) {
      e.preventDefault();
      app.adjustThicknessAtCursor(e);
      return;
    }

    // Ctrl+Shift+wheel with a selection → rotate it. One line: around its centre (or the focused
    // point). Multiple lines: all together around their combined centre. Read-only while comparing.
    if ((e.ctrlKey || e.metaKey) && e.shiftKey && app.selectedIndices().length >= 1 && !app.compareReadOnly()) {
      e.preventDefault();
      const dir = e.deltaY > 0 ? 1 : -1;
      app.rotateSelectedLine(dir * (Math.PI / 60)); // 3° per tick
      return;
    }

    e.preventDefault();

    // Zoom increment scales with the wheel delta so touchpad micro-deltas sum gently; deltaMode
    // normalizes line/page units to pixels, and the cap keeps a big notch from overshooting.
    const px = e.deltaY * (e.deltaMode === 1 ? 16 : e.deltaMode === 2 ? 400 : 1);
    const factor = e.shiftKey ? 0.0095 : 0.004;
    const cap = e.shiftKey ? 0.65 : 0.32;
    const delta = Math.max(-cap, Math.min(cap, -px * factor));
    const sz = smoothZoom;

    // Accumulate target scale (clampScale = the shared [0.05, ZOOM_MAX] zoom bound)
    const base = sz.target !== null ? sz.target : app.scale;
    sz.target = app.zoomPan.clampScale(base + delta);

    // Focal point in image-space (unscaled pixels), through the centring margins (canvasOrigin):
    // with the picture smaller than the frame the scroll origin is not the image origin.
    const vpRect = viewport.getBoundingClientRect();
    const org = canvasOrigin();
    const contentX = e.clientX - vpRect.left + viewport.scrollLeft;
    const contentY = e.clientY - vpRect.top  + viewport.scrollTop;
    sz.focal = {
      imgX: (contentX - org.x) / app.scale,
      imgY: (contentY - org.y) / app.scale,
      // cursor position relative to viewport left/top edge (viewport-local)
      clientX: e.clientX - vpRect.left,
      clientY: e.clientY - vpRect.top,
    };

    // Start animation loop; disable CSS transition first to prevent conflict
    if (!sz.rafId) {
      app.canvas.classList.add('zoom-no-transition');
      sz.rafId = requestAnimationFrame(runSmoothZoom);
    }
  }, { passive: false });
}

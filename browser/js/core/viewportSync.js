import { onWindowResize } from '../utils.js';
import { subscribe, EVENTS } from '../bus/appBus.js';

// ── Viewport height sync ────────────────────────────────────────
// Keeps the canvas viewport (and the coordinates panel, the other column that can outgrow
// the window) sized to the available height on every geometry change. Lifted out of the
// DrawingApp constructor unchanged; the resize leg now rides the shared coalescer.
export function wireViewportSync(app) {
  const syncViewport = () => {
    const vp = document.getElementById('canvas-viewport');
    if (!vp || document.body.classList.contains('fullscreen-mode')) return;
    app.zoomPan.syncViewportHeight(); // the frame always fills the available height
    app.zoomPan.syncCoordPanelHeight();
  };
  syncViewport();
  // …and again after the first paint: the first call runs before the shell's layout is
  // real, which leaves the empty editor slightly too tall (permanent scrollbar).
  if (typeof requestAnimationFrame === 'function') requestAnimationFrame(syncViewport);
  // …and once the shell's reveal ends. The measure already discounts the appReveal
  // translate (zoomPan.js layoutTop), so this only catches anything else it settled.
  const shell = document.querySelector('.container');
  shell?.addEventListener('animationend', (e) => {
    if (e.target === shell && e.animationName === 'appReveal') syncViewport();
  });
  onWindowResize(syncViewport);
  // Docking/undocking the chat moves body's padding (editor height), so re-measure too;
  // deferred a frame so the emitter's class change is in the layout.
  const syncViewportSoon = () => {
    if (typeof requestAnimationFrame === 'function') requestAnimationFrame(syncViewport);
    else syncViewport();
  };
  subscribe(EVENTS.chatLayoutChanged, syncViewportSoon);
  // …and once body's padding finishes ANIMATING (components.css slides it over ~340ms):
  // the events above fire at the start of the slide and measure the old geometry.
  document.body.addEventListener('transitionend', (e) => {
    if (e.target === document.body && e.propertyName.startsWith('padding')) syncViewport();
  });
  // …and once the toolbar fold lands: collapsing it moves the viewport's top by the whole
  // height of the tool rows, so the cap it was given no longer reaches the window bottom.
  document.getElementById('controls-body')?.addEventListener('transitionend', (e) => {
    if (e.propertyName === 'grid-template-rows') syncViewport();
  });
}

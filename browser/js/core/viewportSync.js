import { onWindowResize } from '../utils.js';
import { subscribe, EVENTS } from '../bus/appBus.js';

// Keeps the canvas viewport (and the coordinates panel) sized to the available height on
// every geometry change.
export function wireViewportSync(app) {
  const syncViewport = () => {
    const vp = document.getElementById('canvas-viewport');
    if (!vp || document.body.classList.contains('fullscreen-mode')) return;
    app.zoomPan.syncViewportHeight();
    app.zoomPan.syncCoordPanelHeight();
  };
  syncViewport();
  // Again after the first paint: before the shell's layout is real the empty editor is
  // slightly too tall (permanent scrollbar).
  if (typeof requestAnimationFrame === 'function') requestAnimationFrame(syncViewport);
  // The measure already discounts the appReveal translate (zoomPan.js layoutTop).
  const shell = document.querySelector('.container');
  shell?.addEventListener('animationend', (e) => {
    if (e.target === shell && e.animationName === 'appReveal') syncViewport();
  });
  onWindowResize(syncViewport);
  // Docking the chat moves body's padding; deferred a frame so the class change is in the layout.
  const syncViewportSoon = () => {
    if (typeof requestAnimationFrame === 'function') requestAnimationFrame(syncViewport);
    else syncViewport();
  };
  subscribe(EVENTS.chatLayoutChanged, syncViewportSoon);
  // body's padding ANIMATES (components/chat/touch.css, ~340ms): the events above measure the old geometry.
  document.body.addEventListener('transitionend', (e) => {
    if (e.target === document.body && e.propertyName.startsWith('padding')) syncViewport();
  });
  // Collapsing the toolbar moves the viewport's top by the whole height of the tool rows.
  document.getElementById('controls-body')?.addEventListener('transitionend', (e) => {
    if (e.propertyName === 'grid-template-rows') syncViewport();
  });
}

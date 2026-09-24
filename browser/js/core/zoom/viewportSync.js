import { onWindowResize } from '../../utils.js';
import { subscribe, EVENTS } from '../../eventBus/appBus.js';

// Keeps the canvas viewport (and the coordinates panel) sized to the available height on
// every geometry change.
export function wireViewportSync(app) {
  const vp = document.getElementById('canvas-viewport');
  const syncViewport = () => {
    if (!vp || document.body.classList.contains('fullscreen-mode')) return;
    app.zoomPan.syncViewportHeight();
    app.zoomPan.syncCoordPanelHeight();
  };
  syncViewport();
  // Again after the first paint: before the shell's layout is real the empty editor is
  // slightly too tall (permanent scrollbar).
  if (typeof requestAnimationFrame === 'function') requestAnimationFrame(syncViewport);
  // The measure already discounts the appReveal translate (pan.js layoutTop).
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
  // Every row above or beside the frame moves its room when its height changes (the toolbar's
  // rows for a first picture, the size, status and hint lines), so each one is watched.
  if (vp && typeof ResizeObserver === 'function') {
    const watch = new ResizeObserver(syncViewportSoon);
    const rows = [...(vp.closest('.container')?.children ?? []), ...vp.parentElement.children];
    for (const el of rows) if (el !== vp && !el.contains(vp)) watch.observe(el);
  }
  subscribe(EVENTS.chatLayoutChanged, syncViewportSoon);
  // body's padding ANIMATES (components/chat/touch.css, ~340ms): the events above measure the old geometry.
  document.body.addEventListener('transitionend', (e) => {
    if (e.target === document.body && e.propertyName.startsWith('padding')) syncViewport();
  });
  // Collapsing the toolbar moves the viewport's top by the whole height of the tool rows,
  // so the viewport follows the fold frame by frame rather than jumping when it lands.
  const body = document.getElementById('controls-body');
  let folding = 0;
  const follow = () => {
    syncViewport();
    if (folding) requestAnimationFrame(follow);
  };
  const isFold = (e) => e.target === body && e.propertyName === 'grid-template-rows';
  body?.addEventListener('transitionrun', (e) => {
    if (!isFold(e) || typeof requestAnimationFrame !== 'function') return;
    if (!folding++) requestAnimationFrame(follow);
  });
  const settle = (e) => {
    if (!isFold(e)) return;
    folding = Math.max(0, folding - 1);
    syncViewport();
  };
  body?.addEventListener('transitionend', settle);
  body?.addEventListener('transitioncancel', settle);
}

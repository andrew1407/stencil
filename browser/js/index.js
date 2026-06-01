import { mountHTML } from './utils.js';
import { layout } from './ui/layout.js';
import { DrawingApp } from './core/drawingApp.js';
// ── Application entrypoint ──────────────────────────────────────
// Loaded LAST. Importing layout registers every custom element. On load:
// mount the component hosts (each renders its markup + subscribes to
// `stencil:ready`), construct the app, then dispatch `stencil:ready` so every
// component wires its behavior — preserving the original DOM → app → wire order.
window.onload = () => {
  const root = document.getElementById('root');
  mountHTML(root, layout());      // DOM first (custom elements upgrade synchronously)
  const app = new DrawingApp();   // construct AFTER mount
  window.app = app;
  document.dispatchEvent(new CustomEvent('stencil:ready', { detail: { app } }));
};

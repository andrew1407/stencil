// Editor-page side of the extension's "editor mode": answers `stencil-ext-req` window
// messages (state / import / switch / crop) with `stencil-ext-res`, id-correlated, after
// validating each payload as data. Other half: extension/src/content/editorBridge.js.
import type { DrawingApp } from './drawingApp.js';

/** Listens on `target` (the window; injectable for tests — no target = no-op). */
export declare const wireExtensionBridge: (
  app: DrawingApp,
  target?: Pick<Window, 'addEventListener' | 'postMessage'> | null,
) => void;

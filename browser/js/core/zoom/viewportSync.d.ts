// Keeps the canvas viewport (and the coordinates panel) sized to the available height on
// every geometry change: first paint, the shell reveal, window resize, chat docking, the
// body padding slide and the toolbar fold.
import type { DrawingApp } from '../drawingApp.js';

export declare function wireViewportSync(app: DrawingApp): void;

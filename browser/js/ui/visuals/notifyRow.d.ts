import type { DrawingApp } from '../../core/drawingApp.js';

/** Wires the Visuals & Settings modal's Show-notifications select to notifyChannel.js;
 *  `requestPermission` is the browser's ask, injectable for a test. */
export declare function wireNotifyRow(app: DrawingApp, requestPermission?: () => Promise<string>): { populate(): void; reset(): void };

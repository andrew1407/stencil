import type { DrawingApp } from '../core/drawingApp.js';

/** Sync the topbar project-name field + remote badge. `force` re-syncs even while focused. */
export declare const updateProjectTitle: (app: DrawingApp, force?: boolean) => void;

/** Sync the image-info line (size, blank/incognito tags). */
export declare const updateInfo: (app: DrawingApp) => void;

import type { DrawingApp } from '../../../core/drawingApp.js';

/** Sync the topbar project-name field + remote badge. `force` re-syncs even while focused. */
export declare const updateProjectTitle: (app: DrawingApp, force?: boolean) => void;

/** Paint the name in the picker's colour on trial, until the project or its stored colour changes; null ends it. */
export declare const previewProjectColor: (app: DrawingApp, color: string | null) => void;

/** Sync the image-info line (size, blank/incognito tags). */
export declare const updateInfo: (app: DrawingApp) => void;

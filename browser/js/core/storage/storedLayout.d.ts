// A stored layout back into editor state, in the groups the window-side adapter paints
// between, plus the two shapes a payload with no image takes.
import type { DrawingApp } from '../drawingApp.js';
import type { ProjectLayout } from '../project/projectsStore.js';
import type { Storage } from './storage.js';

/** Custom page size and the unit, then the unit sweep over the UI. */
export declare const applyStoredPage: (app: DrawingApp, layout: ProjectLayout) => void;
/** Colours, thickness, point size, style, visibility and the image filter. */
export declare const applyStoredDrawing: (app: DrawingApp, layout: ProjectLayout) => void;
/** Image name/ext and the provenance URLs, then the tooltip switches. */
export declare const applyStoredProvenance: (app: DrawingApp, layout: ProjectLayout) => void;
/** The allow-formulas gate (synced to the toolbar pill) and the two expressions. */
export declare const applyStoredFormulas: (app: DrawingApp, layout: ProjectLayout) => void;
/** Draw mode, the hold-draw delay and the selection/hover/focus/fill colours. */
export declare const applyStoredTools: (app: DrawingApp, layout: ProjectLayout) => void;
/** Lines kept pending behind the missing-image banner, or settings only. */
export declare const applyImagelessPayload: (storage: Storage, layout: ProjectLayout) => void;

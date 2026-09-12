// The projects-list thumbnail: the EDITED result (filter + lines) as a modest JPEG data URL,
// sized for its largest consumer (the hover zoom), since it lives in the localStorage registry.
import type { DrawingApp } from './drawingApp.js';

/** Synchronous, inline render. null with no image or when the canvas refuses (tainted). */
export declare const makeThumbnail: (app: DrawingApp) => string | null;
/** The same render through the image worker (inline when it is unavailable). */
export declare const renderThumbnail: (app: DrawingApp) => Promise<string | null>;

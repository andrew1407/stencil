// The address bar naming the open project; core/launch/query.js withOpenProject builds the parts.
import type { DrawingApp } from '../../../core/drawingApp.js';

/** The open project as a `project=` value: a saved one's name, else a server-only one's `<server>/projects/<id>`, else null. */
export declare const openProjectRef: (app: DrawingApp) => string | null;
/** Rewrites the URL to name the open project; false when it already does, or in incognito, in a frame or before the launch applies. */
export declare const syncProjectUrl: (app: DrawingApp, win?: Window) => boolean;

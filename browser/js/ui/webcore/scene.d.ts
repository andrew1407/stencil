// The picture and the word an empty editor receives when the skin turns on.
import type { DrawingApp } from '../../core/drawingApp.js';
/** Leave incognito, load the picture as `webcore.png`, install the word; true once the word is down. */
export declare const createWebcoreScene: (app: DrawingApp) => Promise<boolean>;
/** Reopen the local (not server-linked) project named `webcore`; false when there is none. */
export declare const openWebcoreProject: (app: DrawingApp) => boolean;

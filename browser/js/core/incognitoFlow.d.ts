// Incognito: adopting the current turn into an unsaved editor, and the two ways out of it —
// publishing to a server, or keeping it as a local project. Both are explicit user acts.
import type { DrawingApp } from './drawingApp.js';
import type { RemoteLink } from './remoteSyncController.js';

/** Only while the editor is blank: adding content auto-saves. */
export declare const canToggleIncognito: (app: DrawingApp) => boolean;
/** Best-effort report for the projects modal's "Incognito tabs" filter. */
export declare const reportIncognitoSession: (app: DrawingApp) => void;
/** Like openImageHere's incognito branch, but keeps the conversation. */
export declare const adoptIncognitoHere: (app: DrawingApp) => void;
/** Creates the project, pushes layout + result, then LINKS the session. */
export declare const publishIncognitoToServer: (app: DrawingApp, address: string) => Promise<RemoteLink>;
/** The local twin; null when there is nothing to keep. */
export declare const promoteIncognitoToLocal: (app: DrawingApp) => string | null;

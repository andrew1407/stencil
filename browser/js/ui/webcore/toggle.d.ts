// The webcore skin as a session: on, and off again.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { SkinSession } from '../skin/session.js';
/** The skin's session: its name, its off notice, `active` and `toggle`. */
export declare const webcoreSkin: SkinSession;
/** True while `<html data-skin="webcore">` is set. */
export declare const webcoreActive: (doc?: Document) => boolean;
/** Flip the skin; resolves to the state it is left in. `scene: false` never opens the webcore page. */
export declare const toggleWebcore: (app: DrawingApp, doc?: Document,
  opts?: { scene?: boolean }) => Promise<boolean>;

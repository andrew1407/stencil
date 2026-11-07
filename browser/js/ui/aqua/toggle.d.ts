// The aqua skin as a session: on, and off again.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { SkinSession } from '../skin/session.js';
/** The preset the skin opens in for the session (never stored). */
export declare const AQUA_ACCENT: string;
/** The skin's session: its name, its off notice, `active` and `toggle`. */
export declare const aquaSkin: SkinSession;
/** True while `<html data-skin="aqua">` is set. */
export declare const aquaActive: (doc?: Document) => boolean;
/** Flip the skin; resolves to the state it is left in. `scene: false` never opens the wallpaper page. */
export declare const toggleAqua: (app: DrawingApp, doc?: Document,
  opts?: { scene?: boolean }) => Promise<boolean>;

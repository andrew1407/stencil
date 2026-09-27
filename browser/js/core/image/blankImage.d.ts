// Blank projects: a solid-colour raster generated on a scratch canvas and handed to the
// normal load path, so a blank behaves like any other image (lines stay a separate overlay).
import type { DrawingApp } from '../drawingApp.js';

export interface BlankImageOptions {
  /** Fill colour; anything normalizeHex rejects becomes '#ffffff'. */
  color?: string;
  /** Pixels, clamped 1–8192 then shaped to the page aspect; both omitted → the page at 96 dpi. */
  width?: number;
  height?: number;
  /** Also create + link the project on this connected server (ignored while incognito). */
  address?: string;
}

/** Create a blank and load it; resolves with the final size once handed to the load path. */
export declare const createBlankImage: (app: DrawingApp, opts?: BlankImageOptions) => Promise<{ width: number; height: number }>;
/** True while the active session is a blank project (a recolourable solid background). */
export declare const activeIsBlank: (app: DrawingApp) => boolean;
/** Try `color` on the active blank's stage: no reload, undo step, save or push; its own colour or null ends the trial. */
export declare const previewBlankColor: (app: DrawingApp, color: string | null) => void;
/** Recolour the ACTIVE blank in place, keeping every line; a no-op for a non-blank. The trial fill yields to the result. */
export declare const setBlankColor: (app: DrawingApp, color: string) => void;

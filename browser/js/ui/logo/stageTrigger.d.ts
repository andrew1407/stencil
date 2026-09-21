// The one way into a logo show: the mark's hold, a typed word and the facade all arrive here.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { StagePoint } from './stageRules.js';

/** The pink show: a pink page if there is none, the pink tint, and the heart as one step. */
export declare const pinkVibe: (app: DrawingApp) => Promise<boolean>;
/** Gate, run the show, post the notice. False when the window is not bare. */
export declare const activateShow: (name: string, app: DrawingApp, origin?: StagePoint | null)
  => boolean | Promise<boolean>;
/** The show the mark's hold opens right now, or null. */
export declare const heldShow: (app: DrawingApp | null) => string | null;
/** Hold the header mark to open its show; the release never reaches the accent cycle. */
export declare const wireLogoHold: (logo: Element | null, app: DrawingApp,
  opts?: { holdMs?: number }) => void;

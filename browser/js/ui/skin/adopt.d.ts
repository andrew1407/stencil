// A cheat code's look made the user's own stored settings, given back on an off in the same session.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { MotionPrefs } from '../motion/motionPrefs.js';

export interface SkinLookSettings {
  motion: Partial<MotionPrefs>;
  /** The accent preset key the cheat code sets. */
  accent: string;
}
/** Stores the skin's motion and accent; returns the give-back an off runs. */
export declare const adoptLook: (app: DrawingApp, look: SkinLookSettings) => () => void;

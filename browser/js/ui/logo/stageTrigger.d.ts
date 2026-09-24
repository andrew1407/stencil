// The one way into a logo show: the mark's hold, a typed word and the facade all arrive here.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { StagePoint } from './stageRules.js';

/** The pink show: a pink page if there is none, the pink tint, and the heart as one step. */
export declare const pinkVibe: (app: DrawingApp) => Promise<boolean>;
/** Whether `name` is on now: its stage up, the skin stamped, or the pink tint applied. */
export declare const showActive: (name: string, app: DrawingApp | null) => boolean;
/** Closes the stage, every open window and fullscreen. */
export declare const clearWay: (app: DrawingApp | null, doc?: Document) => void;
/** Run the show and post the notice. Gated to the bare window unless `replace` clears the way. */
export declare const activateShow: (name: string, app: DrawingApp, origin?: StagePoint | null,
  opts?: { replace?: boolean; scene?: boolean }) => boolean | Promise<boolean>;
/** The `<name>Mode` switch: on activates (the skin alone for webcore), off closes; idempotent. */
export declare const setShow: (name: string, app: DrawingApp, on: boolean) => void;
/** The show the mark's hold opens right now, or null. */
export declare const heldShow: (app: DrawingApp | null) => string | null;
/** Hold the header mark to open its show; the release never reaches the accent cycle. */
export declare const wireLogoHold: (logo: Element | null, app: DrawingApp,
  opts?: { holdMs?: number }) => void;

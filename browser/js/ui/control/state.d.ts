// The toolbar and panel controls as areas over the editor's change feed (core/app/changes.js):
// each area gates its own controls and follows only the channels its inputs move on, so an edit
// repaints what it touched. updateButtons() runs every area: boot, theme, restore, a new picture.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { ChangeTopic } from '../../core/app/changes.js';

/** One control area: the channels it follows, its gates (before the tooltip pass), its repaint after. */
export interface ControlArea {
  name: string;
  on: readonly ChangeTopic[];
  gate?(app: DrawingApp): void;
  after?(app: DrawingApp): void;
}

/** Every area, in sweep order. */
export declare const AREAS: readonly ControlArea[];
/** The full sweep: every area, one tooltip pass, then the followers. */
export declare function updateButtons(app: DrawingApp): void;
/** `fn` runs after every sweep or flush with the names of the areas that ran; returns the unsubscribe. */
export declare const onButtonsUpdated: (fn: (areas: string[]) => void) => () => boolean;
/** Subscribes every area to its channels on `app.changes`; returns the unsubscribe. */
export declare const wireControlState: (app: DrawingApp) => () => void;

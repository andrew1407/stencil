// The full-window logo stage and the lock that makes it the only thing the editor listens to.
import type { DrawingApp } from '../core/drawingApp.js';
import type { StageEffect, StagePoint } from './logoStageRules.js';

export declare const STAGE_CLASS: string;
/** On <body> while a show is up, so its notice can still stand above the stage. */
export declare const OPEN_CLASS: string;
export declare const logoStageOpen: () => boolean;
/** The header mark's centre, so every way in grows out of the logo. */
export declare const markOrigin: (doc?: Document) => StagePoint | null;
/** True only from the bare window: no stage, no fullscreen, nothing modal on top. */
export declare const logoStageAllowed: (doc?: Document) => boolean;
/** Plays the hide, then drops the element. False when nothing was up. */
export declare const closeLogoStage: () => boolean;
/** Opens `name`'s stage, growing out of `origin` (the header mark's centre). */
export declare const openLogoStage: (name: string,
  opts?: { app?: DrawingApp; origin?: StagePoint | null; doc?: Document }) => boolean;

export interface LiveStage {
  readonly name: string;
  readonly effect: StageEffect;
  readonly host: HTMLElement;
  readonly canvas: HTMLCanvasElement;
  readonly size: number;
  readonly position: StagePoint;
  readonly cloudLive: number;
  release(): void;
}
/** The stage on screen, or null. */
export declare const currentLogoStage: () => LiveStage | null;

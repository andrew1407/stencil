import type { StageApp } from './accents.js';
import type { StageStyle } from './stageRules.js';
export declare const showMotionStyle: () => StageStyle;
export declare const showDustAllowed: () => boolean;
export interface StageLook {
  style: StageStyle | null; dusty: boolean; glows: boolean; img: HTMLImageElement | null;
  cloud: { live: number; step(dt: number, size: number, boost?: number, opts?: object): number;
    draw(ctx: CanvasRenderingContext2D, doc: Document, x: number, y: number, tMs: number, scale?: number): void };
  restyle(): StageLook; refresh(): void; unwatch(): void;
}
export declare const createStageLook: (name: string, app: StageApp, doc: Document) => StageLook;

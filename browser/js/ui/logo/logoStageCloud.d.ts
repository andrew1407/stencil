// The cloud around the stage's mark, over the shared grain kit.
import type { StageStyle, StagePoint } from './logoStageRules.js';

export interface StageMote {
  x: number; y: number; vx: number; vy: number;
  age: number; life: number; r: number; w: number; len: number;
}
/** A mote leaving the edge of a mark of `size`; `reach` widens its flight under a press, and
 *  `dir` (the way the mark travels) lays it behind as a tail instead of all round. */
export declare const newStageMote: (size: number, reach?: number, rnd?: () => number,
  dir?: StagePoint | null) => StageMote;
/** Advance a mote by dt ms; false once its life is spent. */
export declare const stepStageMote: (m: StageMote, dt: number) => boolean;
export declare const stageMoteAlpha: (m: StageMote) => number;
export declare const spawnStageCount: (intensity: number, dt: number, rnd?: () => number) => number;

export interface StageCloud {
  readonly live: number;
  /** `boost` is the stage's: 1 at rest, more under the pointer, most while it is held. */
  step(dt: number, size: number, boost?: number,
    opts?: { dir?: StagePoint | null; rnd?: () => number }): number;
  draw(ctx: CanvasRenderingContext2D, doc: Document, x: number, y: number, tMs: number,
    scale?: number): void;
}
/** One emitter per stage; a null style flies nothing. */
export declare const createStageCloud: (style: StageStyle | null) => StageCloud;

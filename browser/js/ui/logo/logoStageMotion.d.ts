// The logo stage's kinematics, pure and clocked by the caller.
import type { StageConfig, StagePoint } from './logoStageRules.js';

export interface StagePose extends StagePoint { size: number; }
/** The reveal at progress p, from the header mark's pose to the stage's. */
export declare const revealTween: (from: StagePose, to: StagePose, p: number) => StagePose;
/** A mark of `size` kept centred inside w×h. */
export declare const clampCentre: (x: number, y: number, size: number, w: number, h: number) => StagePoint;

export interface BounceState {
  size: number; rest: number; impulse: number;
  phase: 'rest' | 'snap' | 'recover';
  from: number; to: number; t0: number;
}
export declare const bounceState: (rest: number, impulse: number) => BounceState;
/** Re-measure the bounce for a resized window; a snap in flight scales instead of jumping. */
export declare const bounceResize: (st: BounceState, rest: number, impulse: number) => BounceState;
export declare const bounceImpulse: (st: BounceState, now: number) => BounceState;
/** The size at `now`; snaps toward the impulse, then recovers to rest. */
export declare const bounceStep: (st: BounceState, now: number, cfg?: StageConfig['bounce']) => number;

export interface ChaseState extends StagePoint { vx: number; vy: number; }
export declare const chaseState: (x: number, y: number) => ChaseState;
/** Thrown toward the cursor, or away from it, and dragging behind — it never simply arrives. */
export declare const chaseStep: (st: ChaseState, cursor: StagePoint, dt: number, size: number,
  w: number, h: number, cfg: StageConfig['follow'] & Partial<StageConfig['escape']>,
  flee?: boolean) => ChaseState;
/** The way it travels, as a unit vector; null once it is slower than `minSpeed`, so a mark
 *  the cursor has caught wears a ring of grain instead of a tail. */
export declare const headingOfState: (st: { vx: number; vy: number }, minSpeed?: number) => StagePoint | null;

export interface FlyState extends StagePoint { vx: number; vy: number; }
export declare const flyState: (x: number, y: number, angle: number, cfg?: StageConfig['fly']) => FlyState;
/** A punch: the velocity re-aimed at `angle` with the punch speed on top. */
export declare const flyPunch: (st: FlyState, angle: number, cfg?: StageConfig['fly']) => FlyState;
export declare const flyStep: (st: FlyState, dt: number, size: number, w: number, h: number,
  cfg?: StageConfig['fly']) => FlyState;

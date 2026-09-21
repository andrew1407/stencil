// Hold-to-draw: the pure target decision and the time-injected gesture state machine
// (idle → armed → drawing → idle, or armed → aborted). The host owns timers, coordinate
// conversion and rendering. C++ mirror: core/holdDraw; wasm twin in coreHandles.d.ts.
import type { CodecLine } from '../line/linesCodec.js';
import type { HoldEvent, HoldDrawOptions } from '../abi/coreHandles.js';

/** What an initial hold over (x, y) targets; ptIdx / ptIdx2 are -1 when not applicable. */
export interface HoldTarget {
  kind: 'point' | 'segment' | 'new';
  lineIdx: number;
  ptIdx: number;
  ptIdx2: number;
}

/** Topmost line wins: a point hit beats a segment hit beats empty space. */
export declare const holdDrawTarget: (
  lines: readonly Pick<CodecLine, 'points'>[] | null | undefined,
  x: number, y: number,
  opts?: { pointThreshold?: number; segThreshold?: number },
) => HoldTarget;

/** Coordinates are host client space; times any monotonic ms. The wasm twin adds destroy(). */
export declare class HoldDrawController {
  constructor(opts?: HoldDrawOptions);
  readonly state: 'idle' | 'armed' | 'drawing' | 'aborted';
  readonly active: boolean;
  readonly engaged: boolean;
  readonly holdDelay: number;
  setHoldDelay(ms: number): void;
  cancel(): void;
  pointerDown(x: number, y: number, t: number): HoldEvent | null;
  pointerMove(x: number, y: number, t: number): HoldEvent | null;
  tick(t: number): HoldEvent | null;
  pointerUp(t: number): HoldEvent | null;
}

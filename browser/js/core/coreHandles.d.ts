/** The event objects the hold-draw machine returns — the exact shapes holdDraw.js emits. */
export type HoldEvent =
  | { type: 'armed' | 'abort' | 'commit' }
  | { type: 'start' | 'drop' | 'preview'; x: number; y: number };

/** The wasm twin of holdDraw.js's HoldDrawController; destroy() releases its core handle. */
export interface WasmHoldDrawController {
  readonly state: 'idle' | 'armed' | 'drawing' | 'aborted';
  readonly active: boolean;
  readonly engaged: boolean;
  readonly holdDelay: number;
  setHoldDelay(ms: number): void;
  cancel(): void;
  destroy(): void;
  pointerDown(x: number, y: number, t: number): HoldEvent | null;
  pointerMove(x: number, y: number, t: number): HoldEvent | null;
  tick(t: number): HoldEvent | null;
  pointerUp(t: number): HoldEvent | null;
}

export interface HoldDrawOptions { holdDelay?: number; moveTolerance?: number; rearmDistance?: number; }

/** Built over an instantiated Emscripten module; keyed like the pure ops in stencilCore.js. */
export function buildHandleClasses(mod: unknown): {
  HoldDrawController: new (opts?: HoldDrawOptions) => WasmHoldDrawController;
};

/** C export symbols the handle classes cwrap, verified before any wrapper installs. */
export const handleExports: string[];

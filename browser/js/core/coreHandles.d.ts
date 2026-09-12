import type { CodecLine } from './linesCodec';

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

/** The wasm twin of historyStack.js's HistoryStack; destroy() releases its core handle. */
export interface WasmHistoryStack {
  readonly historyStep: number;
  readonly size: number;
  reset(lines: readonly Partial<CodecLine>[], baseStep?: number): void;
  push(lines: readonly Partial<CodecLine>[]): void;
  canUndo(): boolean;
  canRedo(): boolean;
  undo(): CodecLine[] | null;
  redo(): CodecLine[] | null;
  destroy(): void;
}

export interface HoldDrawOptions { holdDelay?: number; moveTolerance?: number; rearmDistance?: number; }

/** Built over an instantiated Emscripten module; keyed like the pure ops in stencilCore.js. */
export function buildStateOps(mod: unknown): {
  HoldDrawController: new (opts?: HoldDrawOptions) => WasmHoldDrawController;
  HistoryStack: new () => WasmHistoryStack;
};

/** C export symbols these ops cwrap, verified before any wrapper installs. */
export const stateExports: string[];

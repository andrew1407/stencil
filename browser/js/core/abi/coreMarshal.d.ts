// Heap marshalling for the wasm core's raw exports, built once per loaded module: the
// core*Ops modules take it so each op only names its own cwrap signatures.
import type { XY } from './stencilCore.js';
import type { CropRect } from '../geometry.js';

/** A malloc'd flat f64 point buffer; `view` aliases it until memory growth detaches it. */
export interface PointBuffer {
  ptr: number;
  n: number;
  view: Float64Array;
}

export interface CoreMarshal {
  /** sizeof a double, an int64 and an int32 in the core's heap. */
  readonly F64: number;
  readonly I64: number;
  readonly I32: number;
  /** Runs `fill` against a 4-double out slot and reads it back as a rect. */
  withRectOut(fill: (out: number) => void): CropRect;
  allocPoints(points: readonly XY[]): PointBuffer;
  /** The string crosses on the HEAP, not cwrap's ~64KB stack. */
  withCString<T>(str: string | null | undefined, fn: (ptr: number, byteLength: number) => T): T;
  /** The one image-sized buffer, grown on demand; the pointer is reused every call. A failed grow
   * throws a CoreHeapError and caches nothing. */
  pixelScratch(bytes: number): number;
}

/** The core's heap could not grow: `_malloc` answered 0. */
export declare class CoreHeapError extends Error {
  constructor(bytes: number);
}
/** `_malloc(bytes)`, or a CoreHeapError instead of a null pointer. */
export declare const heapAlloc: (core: { _malloc(bytes: number): number }, bytes: number) => number;
/** One buffer per size; a failure frees those already taken, then throws. */
export declare const heapAllocAll: (core: { _malloc(bytes: number): number; _free(ptr: number): void },
  sizes: readonly number[]) => number[];
/** Each op wrapped so a CoreHeapError calls `onExhausted` before it propagates. */
export declare const guardHeapOps: <T extends Record<string, (...args: never[]) => unknown>>(ops: T,
  onExhausted: (err: CoreHeapError) => void) => T;
/** The op `getFn()` returns, else `jsRef`; a CoreHeapError from the op is answered by `jsRef`. */
export declare const withJsFallback: <F extends (...args: never[]) => unknown>(getFn: () => F | null | undefined, jsRef: F) => F;
export declare const createMarshal: (core: unknown) => CoreMarshal;

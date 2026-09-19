// The shared C++ core singleton: owns the wasm build and typed wrappers over its raw
// extern "C" exports. The artifact is generated and may be absent, so init() imports it
// dynamically and degrades to the JS fallbacks; Node never loads wasm.
import type { CropRect } from './geometry.js';
import type { HoldDrawOptions, WasmHoldDrawController, WasmHistoryStack } from './coreHandles.js';

export interface XY { x: number; y: number; }
export type FilterMode = 'none' | 'bw' | 'sepia' | 'custom' | 'invert' | 'contour';

/** The wasm op map, keyed like the JS reference implementations each one replaces. */
export interface CoreOps {
  parseHex(hex: string): { r: number; g: number; b: number } | null;
  distToSegment(px: number, py: number, a: XY, b: XY): number;
  formulaValidate(expr: string, varName: string): boolean;
  formulaApply(expr: string, varName: string, val: number, allowFormulas: boolean): number;
  /** ms, 0 = keep forever; null when invalid. */
  parseDuration(spec: string | null | undefined): number | null;
  clampScale(scale: number): number;
  shouldCloseShape(points: readonly XY[], click: XY, pointSize: number): boolean;
  pageDimensions(name: string, cw: number, ch: number, customW: number, customH: number): { width: number; height: number };
  /** Space-separated canonical page-format names, no "custom". */
  pageFormats(): string;
  pixelToPageRaw(x: number, y: number, dims: { width: number; height: number }, cw: number, ch: number): XY;
  /** Mutates `points` in place. */
  rotatePoints(points: XY[], cx: number, cy: number, angle: number): void;
  flipPoints(points: XY[], horizontal: boolean, cx: number, cy: number): void;
  boundingBoxCenter(points: readonly XY[]): XY;
  /** Mutates `data` in place; r/g/b tint the custom mode. */
  applyFilterRGBA(mode: FilterMode, data: Uint8ClampedArray | Uint8Array, pixelCount: number, r: number, g: number, b: number): void;
  applyContourRGBA(data: Uint8ClampedArray | Uint8Array, width: number, height: number): void;
  isAlbumOrientation(w: number, h: number): boolean;
  cropAspect(pageWidth: number, pageHeight: number, album: boolean): number;
  centeredCrop(imageW: number, imageH: number, aspectWoverH: number): CropRect;
  resizeCropFromCorner(cur: CropRect, corner: number, cursorX: number, cursorY: number, aspectWoverH: number, imageW: number, imageH: number, minSize?: number): CropRect;
  moveCropClamped(cur: CropRect, dx: number, dy: number, imageW: number, imageH: number): CropRect;
  scaleCropCentered(cur: CropRect, factor: number, aspectWoverH: number, imageW: number, imageH: number): CropRect;
  swapCropOrientation(cur: CropRect, aspectWoverH: number, imageW: number, imageH: number): CropRect;
  cropResizeScale(oldWidth: number, newWidth: number): number;
  cropChange(oldRect: CropRect, newRect: CropRect): { orientationChanged: boolean; scale: number };
  rotateCropRectQuarter(r: CropRect, imageW: number, imageH: number, clockwise: boolean): CropRect;
  HoldDrawController: new (opts?: HoldDrawOptions) => WasmHoldDrawController;
  HistoryStack: new () => WasmHistoryStack;
}

export type CoreOpName = keyof CoreOps
  | 'projectPeriodMs' | 'projectAddPeriod' | 'projectShouldPersist' | 'projectIsExpired' | 'projectIsExpiringSoon';

export interface StencilCore {
  /** True once the compiled core is live. */
  readonly ready: boolean;
  /** Idempotent; resolves false (JS fallback stays) when the artifact is missing or stale. */
  init(): Promise<boolean>;
  /** Routes to the wasm op when installed, else `jsRef` — read per call, so a boot-time bind picks up the swap. */
  bind<F extends (...args: never[]) => unknown>(name: CoreOpName, jsRef: F): F;
  /** The installed wasm fn, or null — for asymmetric callers with their own fallback. */
  op<K extends keyof CoreOps>(name: K): CoreOps[K] | null;
  readonly opNames: CoreOpName[];
}

export declare const core: StencilCore;

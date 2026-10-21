import type { Cast } from './shot/cast.js';

export interface GifLook {
  fps?: number; width?: number; colors?: number; scale?: string; firstMs?: number; lastMs?: number;
  cropPx?: { x: number; y: number; width: number; height: number } | null;
}

export function framesToGif(dir: string, dst: string, opts?: GifLook & { inFps?: number }): string;
export function castToGif(cast: Cast, dst: string, opts?: GifLook): string;
export function pairToGif(left: Cast, right: Cast, dst: string, opts?: GifLook & { gap?: number }): string;
export function dropBand(file: string, top: number, bottom: number): string;
export function quantizePng(file: string): string;

// The aqua skin's table and the pure rules over it (config/aqua.json).
import type { WordLine } from '../webcore/rules.js';
export interface AquaSwoosh {
  x0: number; span: number; base: number; amp: number; width: number; edge: number;
  color: string; edgeColor: string;
}
export interface AquaConfig {
  tokens: { light: Record<string, string>; dark: Record<string, string> };
  tint: { base: string; light: Record<string, [string, number]>; dark: Record<string, [string, number]> };
  font: { families: string[]; px: number; pt: number };
  image: { width: number; height: number; cell: number; ramp: string[]; swooshes: AquaSwoosh[] };
  word: { stroke: string; thickness: number; colors: string[]; widthShare: number; centerYShare: number };
  strings: { off: string; project: string; imageName: string };
}
export declare const AQUA: AquaConfig;
/** The value the skin stamps on `<html data-skin>`. */
export declare const SKIN_NAME: string;
export declare const OFF_TOAST: string;
export declare const PROJECT_NAME: string;
export declare const IMAGE_NAME: string;

/** `a` moved toward `b` by `share` (0–1) in sRGB, as CSS color-mix(in srgb) gives. */
export declare const mixHex: (a: string, b: string, share: number) => string;
/** The gel tokens (--aq-gel-*, --aq-select, --aq-on-select, --aq-on-gel, --aq-focus, --aq-link) `accent` paints on a face. */
export declare const gelTokens: (accent: string | null | undefined, dark?: boolean) => Record<string, string>;

export interface WallGrid { cell: number; cols: number; rows: number; }
export declare const wallGrid: () => WallGrid;
export declare const rampAt: (cy: number, grid: WallGrid) => string;
/** Rows below swoosh s's crest at (cx, cy), or -1 outside it. */
export declare const swooshDepth: (s: AquaSwoosh, cx: number, cy: number) => number;
export declare const wallColorAt: (cx: number, cy: number, grid: WallGrid) => string;
export interface WallRun { x: number; y: number; w: number; h: number; color: string; }
/** Same-colour runs along every row of the wallpaper. */
export declare const wallRuns: (grid: WallGrid) => WallRun[];
/** STENCIL as seven closed, locked, gel-filled lines across the top of a w×h image. */
export declare const wordLines: (w: number, h: number) => WordLine[];
